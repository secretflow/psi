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

#include "psi/psi/psi21_experiment/el_mp_psi/el_sender.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "psi/psi/psi21_experiment/el_mp_psi/el_hashing.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/ro.h"
#include "yacl/kernel/algorithms/base_ot.h"
#include "yacl/kernel/algorithms/iknp_ote.h"
#include "yacl/kernel/algorithms/kkrt_ote.h"
#include "yacl/link/link.h"
#include "yacl/utils/serialize.h"

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

std::vector<uint128_t> ElRecv(
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

  uint128_t nonce = yacl::DeserializeUint128(
      ctx->Recv(ctx->NextRank(), "Receive OPPRF nonce"));
  uint128_t xssize = yacl::DeserializeUint128(
      ctx->Recv(ctx->NextRank(), "Receive OPPRF Xssize"));
  auto buf = ctx->Recv(ctx->NextRank(), "Receive OPPRF EncryptionTable");
  std::vector<uint128_t> table(xssize);
  std::memcpy(table.data(), buf.data(), table.size() * sizeof(uint128_t));
  // todo
  for (size_t i{}; i != table.size(); ++i) {
    table[i] = table[i] + (nonce >> 10);
    // SPDLOG_INFO(" table[i] = {}, size{}",
    // table[i], xssize);
  }

  return table;
}

void ElSend(const std::shared_ptr<yacl::link::Context>& ctx,
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
  std::vector<uint128_t> zs;
  sender.Init(ctx, store, num_ot);
  sender.SetBatchSize(BATCH_SIZE);

  uint128_t nonce;
  nonce = yc::FastRandSeed();
  for (size_t i{}; i != xs.size(); ++i) {
    zs.push_back(xs[i] - (nonce >> 10));
  }
  ctx->SendAsync(ctx->NextRank(), yacl::SerializeUint128(nonce),
                 fmt::format("OPPRF:Nonce={}", nonce));
  uint128_t xssize = zs.size();
  ctx->SendAsync(ctx->NextRank(), yacl::SerializeUint128(xssize),
                 fmt::format("OPPRF:Xssize={}", xs.size()));
  yacl::Buffer buf(zs.data(), zs.size() * sizeof(uint128_t));
  ctx->SendAsync(ctx->NextRank(), buf, "OPPRF:EncryptionTable");
}

}  // namespace psi::psi
