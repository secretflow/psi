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

#include "experimental/psi/psi21/el_mp_psi/el_mp_psi.h"

#include <future>

#include "experimental/psi/psi21/el_mp_psi/el_protocol.h"
#include "experimental/psi/psi21/el_mp_psi/el_sender.h"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/serialize.h"

#include "psi/utils/communication.h"
#include "psi/utils/sync.h"

namespace psi::psi {

namespace {

constexpr uint32_t kLinkRecvTimeout = 60 * 60 * 1000;

}  // namespace

NmpParty::NmpParty(const Options& options) : options_{options} {
  auto [ctx, wsize, me, leader] = CollectContext();
  ctx->SetRecvTimeout(kLinkRecvTimeout);
  p2p_.resize(wsize);
  for (size_t dst{}; dst != wsize; ++dst) {
    if (me != dst) {
      p2p_[dst] = CreateP2PLinkCtx("el_mp_psi", ctx, dst);
    }
  }
}

// template <class FieldType>
std::vector<std::string> NmpParty::Run(const std::vector<std::string>& inputs) {
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
  auto recv_share = ConvertShares(items, count, shares);
  auto recons = Reconstruct(items, count, recv_share);
  std::vector<std::string> intersection;
  for (size_t i{}; i != recons.size(); ++i) {
    // SPDLOG_INFO("recons[i] = {}, size{}, i{}",
    // recons[i], recons.size(), i);
    std::stringstream ss;
    ss << recons[i];
    intersection.push_back(ss.str());
  }

  randomParse(items, count, shares, p2p_, me, wsize, M, N);

  return intersection;
}

std::vector<uint128_t> NmpParty::EncodeInputs(
    const std::vector<std::string>& inputs, size_t count) const {
  std::vector<uint128_t> items;
  items.reserve(count);
  std::transform(inputs.begin(), inputs.end(), std::back_inserter(items),
                 [](std::string_view input) {
                   /* SPDLOG_INFO("input  {},encode  {} size {}",input,
                      yacl::crypto::Blake3_128(input), input.size()); */
                   return yacl::crypto::Blake3_128(input);
                 });

  std::generate_n(std::back_inserter(items), count - inputs.size(),
                  yacl::crypto::FastRandU128);
  return items;
}

auto NmpParty::ZeroSharing(size_t count) const -> std::vector<Share> {
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

std::vector<uint128_t> NmpParty::ConvertShares(
    const std::vector<uint128_t>& items, size_t count,
    const std::vector<Share>& shares) const {
  count = count + 1;
  auto [ctx, wsize, me, leader] = CollectContext();
  std::vector<uint128_t> recv_shares(count);

  for (size_t id{}; id != me; ++id) {
    ElSend(p2p_[id], items, shares[id]);
    return ElRecv(p2p_[id], items);
  }

  for (size_t id{me + 1}; id != wsize; ++id) {
    auto ret = ElRecv(p2p_[id], items);
    ElSend(p2p_[id], items, shares[id]);
    return ret;
  }

  return recv_shares;
}

std::vector<uint128_t> findSame(const std::vector<uint128_t>& nLeft,
                                const std::vector<uint128_t>& nRight) {
  std::vector<uint128_t> nResult;
  for (std::vector<uint128_t>::const_iterator nIterator = nLeft.begin();
       nIterator != nLeft.end(); nIterator++) {
    if (std::find(nRight.begin(), nRight.end(), *nIterator) != nRight.end()) {
      nResult.push_back(0);
    } else {
      nResult.push_back(*nIterator);
    }
  }

  return nResult;
}

std::vector<uint128_t> NmpParty::Reconstruct(
    const std::vector<uint128_t>& items, size_t count,
    const std::vector<uint128_t>& shares) const {
  count = count + 1;
  return findSame(items, shares);
}
}  // namespace psi::psi
