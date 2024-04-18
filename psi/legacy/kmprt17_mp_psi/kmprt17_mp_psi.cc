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

#include "psi/legacy/kmprt17_mp_psi/kmprt17_mp_psi.h"

#include <future>

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/serialize.h"

#include "psi/legacy/kmprt17_mp_psi/kmprt17_opprf.h"
#include "psi/utils/communication.h"
#include "psi/utils/sync.h"

namespace psi::psi {

namespace {

constexpr uint32_t kLinkRecvTimeout = 60 * 60 * 1000;

}  // namespace

KmprtParty::KmprtParty(const Options& options) : options_{options} {
  auto [ctx, wsize, me, leader] = CollectContext();
  ctx->SetRecvTimeout(kLinkRecvTimeout);
  p2p_.resize(wsize);
  for (size_t dst{}; dst != wsize; ++dst) {
    if (me != dst) {
      p2p_[dst] = CreateP2PLinkCtx("kmprt17_mp_psi", ctx, dst);
    }
  }
}

std::vector<std::string> KmprtParty::Run(
    const std::vector<std::string>& inputs) {
  auto [ctx, wsize, me, leader] = CollectContext();
  auto counts = AllGatherItemsSize(ctx, inputs.size());
  size_t count{};
  for (auto cnt : counts) {
    if (cnt == 0) {
      return {};
    }
    count = std::max(cnt, count);
  }
  auto items = EncodeInputs(inputs, count);
  auto shares = ZeroSharing(count);
  auto recv_share = SwapShares(items, shares);
  auto recons = Reconstruct(items, recv_share);
  std::vector<std::string> intersection;
  for (size_t k{}; k != count; ++k) {
    if (recons[k] == 0) {
      intersection.emplace_back(inputs[k]);
    }
  }
  return intersection;
}

std::vector<uint128_t> KmprtParty::EncodeInputs(
    const std::vector<std::string>& inputs, size_t count) const {
  std::vector<uint128_t> items;
  items.reserve(count);
  std::transform(
      inputs.begin(), inputs.end(), std::back_inserter(items),
      [](std::string_view input) { return yacl::crypto::Blake3_128(input); });
  //  Add random dummy elements
  std::generate_n(std::back_inserter(items), count - inputs.size(),
                  yacl::crypto::FastRandU128);
  return items;
}

auto KmprtParty::ZeroSharing(size_t count) const -> std::vector<Share> {
  auto [ctx, wsize, me, leader] = CollectContext();
  std::vector<Share> shares(wsize, Share(count));
  for (size_t k{}; k != count; ++k) {
    uint64_t sum{};
    for (size_t dst{1}; dst != wsize; ++dst) {
      sum ^= shares[dst][k] = yacl::crypto::FastRandU64();
    }
    shares[0][k] = sum;
  }
  return shares;
}

auto KmprtParty::SwapShares(const std::vector<uint128_t>& items,
                            const std::vector<Share>& shares) const -> Share {
  auto [ctx, wsize, me, leader] = CollectContext();
  auto count = shares.front().size();
  std::vector<Share> recv_shares(count);
  std::vector<std::future<Share>> futures(wsize);
  // NOTE: First Send Then Receive for peers of smaller ranks
  for (size_t id{}; id != me; ++id) {
    futures[id] = std::async(
        [&](size_t id) {
          KmprtOpprfSend(p2p_[id], items, shares[id]);
          return KmprtOpprfRecv(p2p_[id], items);
        },
        id);
  }
  // NOTE: First Receive Then Send for peers of larger ranks
  for (size_t id{me + 1}; id != wsize; ++id) {
    futures[id] = std::async(
        [&](size_t id) {
          auto ret = KmprtOpprfRecv(p2p_[id], items);
          KmprtOpprfSend(p2p_[id], items, shares[id]);
          return ret;
        },
        id);
  }
  for (size_t id{}; id != wsize; ++id) {
    recv_shares[id] = (me == id ? shares[id] : futures[id].get());
  }

  Share share(count);  // S(x_k)
  for (size_t k{}; k != count; ++k) {
    for (size_t src{}; src != wsize; ++src) {
      share[k] ^= recv_shares[src][k];
    }
  }
  return share;
}

auto KmprtParty::Reconstruct(const std::vector<uint128_t>& items,
                             const Share& share) const -> Share {
  auto [ctx, wsize, me, leader] = CollectContext();
  auto count = items.size();
  if (me == leader) {
    std::vector<Share> recv_shares(count);
    std::vector<std::future<Share>> futures(wsize);
    for (size_t src{}; src != wsize; ++src) {
      if (me != src) {
        futures[src] = std::async(
            [&](size_t src) { return KmprtOpprfRecv(p2p_[src], items); }, src);
      }
    }
    for (size_t src{}; src != wsize; ++src) {
      recv_shares[src] = (me == src ? share : futures[src].get());
    }
    Share recons(count);  // sum of S_i(x_k) over i
    for (size_t k{}; k != count; ++k) {
      for (size_t src{}; src != wsize; ++src) {
        recons[k] ^= recv_shares[src][k];
      }
    }
    return recons;
  } else {
    KmprtOpprfSend(p2p_[leader], items, share);
    return share;
  }
}

}  // namespace psi::psi
