// Copyright 2024 zhangwfjh
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "psi/legacy/kmprt17_mp_psi/kmprt17_opprf.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/ro.h"
#include "yacl/kernel/algorithms/base_ot.h"
#include "yacl/kernel/algorithms/iknp_ote.h"
#include "yacl/kernel/algorithms/kkrt_ote.h"
#include "yacl/link/link.h"
#include "yacl/utils/serialize.h"

#include "psi/legacy/kmprt17_mp_psi/kmprt17_hashing.h"

namespace psi::psi {

namespace yc = yacl::crypto;

namespace {

// PSI-related constants
// Ref https://eprint.iacr.org/2017/799.pdf (Table 2)
constexpr float ZETA[]{1.12f, 0.17f};
// constexpr size_t BETA[]{31, 63};
constexpr size_t TABLE_SIZE[]{32, 64};  // next power of BETAs

// OTe-related constants
constexpr size_t NUM_BASE_OT{128};
constexpr size_t NUM_INKP_OT{512};
constexpr size_t BATCH_SIZE{896};

static auto ro = yc::RandomOracle::GetDefault();

}  // namespace

// Ref https://eprint.iacr.org/2017/799.pdf (Figure 6, 7)

std::vector<uint64_t> KmprtOpprfRecv(
    const std::shared_ptr<yacl::link::Context>& ctx,
    const std::vector<uint128_t>& queries) {
  const size_t size{queries.size()};
  const size_t bin_sizes[]{static_cast<size_t>(std::ceil(size * ZETA[0])),
                           static_cast<size_t>(std::ceil(size * ZETA[1]))};
  // Step 0. Prepares OPRF
  yc::KkrtOtExtReceiver receiver;
  size_t num_ot{bin_sizes[0] + bin_sizes[1]};
  auto choice = yc::RandBits(NUM_BASE_OT);
  auto base_ot = yc::BaseOtRecv(ctx, choice, NUM_BASE_OT);
  auto store = yc::IknpOtExtSend(ctx, base_ot, NUM_INKP_OT);
  receiver.Init(ctx, store, num_ot);
  receiver.SetBatchSize(BATCH_SIZE);

  // Step 1. Hashes queries into Cuckoo hashing
  KmprtCuckooHashing hashing{bin_sizes[0], bin_sizes[1]};
  for (size_t i{}; i != size; ++i) {
    hashing.Insert(queries[i]);
  }

  std::vector<uint128_t> evals;
  evals.reserve(num_ot);
  size_t ot_idx{}, b{};
  std::array<uint64_t, BATCH_SIZE> batch_evals;
  // Step 2. For each bin, invokes single-query OPPRF
  for (uint8_t c{}; c != 2; ++c) {
    size_t ot_begin{c == uint8_t{0} ? 0 : bin_sizes[0]};
    size_t ot_end{c == uint8_t{0} ? bin_sizes[0] : num_ot};
    for (size_t addr{}; addr != hashing.num_bins_[c]; ++addr, ++ot_idx) {
      auto elem = hashing.GetBin(c, addr);
      elem == KmprtCuckooHashing::NONE && (elem = yc::FastRandU128());
      receiver.Encode(
          ot_idx, elem,
          {reinterpret_cast<uint8_t*>(&batch_evals[b++]), sizeof(uint64_t)});
      if (auto batch_size = (ot_idx - ot_begin) % BATCH_SIZE + 1;
          batch_size == BATCH_SIZE || ot_idx + 1 == ot_end) {
        b = 0;
        receiver.SendCorrection(ctx, batch_size);
        // For each query in a batch
        for (size_t i{}; i != batch_size; ++i) {
          uint128_t nonce = yacl::DeserializeUint128(
              ctx->Recv(ctx->NextRank(), "Receive OPPRF nonce"));
          std::vector<uint64_t> table(TABLE_SIZE[c]);
          auto buf =
              ctx->Recv(ctx->NextRank(), "Receive OPPRF EncryptionTable");
          std::memcpy(table.data(), buf.data(),
                      TABLE_SIZE[c] * sizeof(uint64_t));
          uint64_t eval = batch_evals[i];
          auto index =
              ro.Gen<size_t>(absl::MakeSpan(reinterpret_cast<uint8_t*>(&eval),
                                            sizeof eval),
                             nonce) %
              table.size();
          evals.emplace_back(eval ^ table[index]);
        }
      }
    }
  }

  // Step 3. Filters and obtains the results
  std::vector<uint64_t> results(size);
  std::transform(queries.cbegin(), queries.cend(), results.begin(),
                 [&](auto q) {
                   auto [c, addr] = hashing.Lookup(q);
                   return evals[c * bin_sizes[0] + addr];
                 });
  return results;
}

void KmprtOpprfSend(const std::shared_ptr<yacl::link::Context>& ctx,
                    const std::vector<uint128_t>& xs,
                    const std::vector<uint64_t>& ys) {
  YACL_ENFORCE_EQ(xs.size(), ys.size(), "Sizes mismatch.");
  const size_t size{xs.size()};
  const size_t bin_sizes[]{static_cast<size_t>(std::ceil(size * ZETA[0])),
                           static_cast<size_t>(std::ceil(size * ZETA[1]))};
  // Step 0. Prepares OPRF
  yc::KkrtOtExtSender sender;
  size_t num_ot{bin_sizes[0] + bin_sizes[1]};
  auto base_ot = yc::BaseOtSend(ctx, NUM_BASE_OT);
  auto choice = yc::RandBits(NUM_INKP_OT);
  auto store = yc::IknpOtExtRecv(ctx, base_ot, choice, NUM_INKP_OT);
  sender.Init(ctx, store, num_ot);
  sender.SetBatchSize(BATCH_SIZE);

  // Step 1. Hashes points into Simple hashing
  KmprtSimpleHashing hashing{bin_sizes[0], bin_sizes[1]};
  for (size_t i{}; i != size; ++i) {
    hashing.Insert({xs[i], ys[i]});
  }
  size_t ot_idx{};
  auto evaluator = sender.GetOprf();
  // Step 2. For each bin, invokes single-query OPPRF
  for (uint8_t c{}; c != 2; ++c) {
    size_t ot_begin{c == uint8_t{0} ? 0 : bin_sizes[0]};
    size_t ot_end{c == uint8_t{0} ? bin_sizes[0] : num_ot};
    // For each programmable point in a batch
    for (size_t addr{}; addr != hashing.num_bins_[c]; ++addr, ++ot_idx) {
      if ((ot_idx - ot_begin) % BATCH_SIZE == 0) {
        sender.RecvCorrection(
            ctx, ot_idx + BATCH_SIZE >= ot_end ? ot_end - ot_idx : BATCH_SIZE);
      }
      auto bin = hashing.GetBin(c, addr);
      uint128_t nonce;
      std::vector<uint64_t> table;
      bool separable;
      do {
        separable = true;
        nonce = yc::FastRandSeed();
        table.assign(TABLE_SIZE[c], uint64_t{0});
        for (auto it = bin.cbegin(); it != bin.cend(); ++it) {
          uint64_t eval = evaluator->Eval(ot_idx, it->first);
          auto index =
              ro.Gen<size_t>({reinterpret_cast<uint8_t*>(&eval), sizeof eval},
                             nonce) %
              table.size();
          if (table[index] != uint64_t{0}) {
            separable = false;
            break;
          }
          table[index] = eval ^ it->second;
        }
      } while (!separable);
      for (size_t i{}; i != TABLE_SIZE[c]; ++i) {
        table[i] == uint64_t{0} && (table[i] = yc::FastRandU128());
      }
      ctx->SendAsync(ctx->NextRank(), yacl::SerializeUint128(nonce),
                     fmt::format("OPPRF:Nonce={}", nonce));
      yacl::Buffer buf(table.data(), table.size() * sizeof(uint64_t));
      ctx->SendAsync(ctx->NextRank(), buf, "OPPRF:EncryptionTable");
    }
  }
}

}  // namespace psi::psi
