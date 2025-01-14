// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/algorithm/nty/nty_mp_psi.h"

#include <unistd.h>

#include "sparsehash/dense_hash_map"
#include "yacl/crypto/aes/aes_intrinsics.h"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/serialize.h"

#include "psi/algorithm/rr22/okvs/baxos.h"
#include "psi/algorithm/rr22/rr22_utils.h"
#include "psi/algorithm/rr22/sparseconfig.h"
#include "psi/utils/communication.h"
#include "psi/utils/sync.h"

namespace psi::nty {

namespace {
// Statistical security parameter
constexpr size_t kDefaultSSP = 40;

struct NoHash {
  inline size_t operator()(const uint128_t& v) const {
    uint32_t v32;
    std::memcpy(&v32, &v, sizeof(uint32_t));

    return v32;
  }
};

}  // namespace

NtyMPsi::NtyMPsi(const NtyMPsiOptions& options) : options_{options} {
  YACL_ENFORCE(options_.aided_server_rank != options_.helper_rank &&
               options_.aided_server_rank != options_.receiver_rank &&
               options_.helper_rank != options_.receiver_rank);
  YACL_ENFORCE(options_.link_ctx != nullptr, "link_ctx should be initialized");
  options_.num_threads = std::max<size_t>(1, options_.num_threads);
  auto [ctx, wsize, self_rank] = CollectContext();
  YACL_ENFORCE(options_.aided_server_rank < wsize &&
               options_.helper_rank < wsize && options_.receiver_rank < wsize);
  YACL_ENFORCE(wsize >= 3,
               "the number of participants should be larger than 3");
  p2p_.resize(wsize);
  for (size_t dst = 0; dst < wsize; ++dst) {
    if (self_rank != dst) {
      p2p_[dst] = CreateP2PLinkCtx("nty_mpsi", ctx, dst);
    }
  }
  SPDLOG_DEBUG("party{} create p2p link success", self_rank);
}

std::vector<uint128_t> NtyMPsi::Run(const std::vector<uint128_t>& inputs) {
  auto [ctx, wsize, self_rank] = CollectContext();
  std::vector<uint128_t> intersection;
  if (self_rank == options_.receiver_rank) {
    SPDLOG_INFO("aided_server_rank is {}", options_.aided_server_rank);
    SPDLOG_INFO("helper_rank is {}", options_.helper_rank);
    SPDLOG_INFO("receiver_rank is {}", options_.receiver_rank);
  }
  if (wsize == 3) {
    SPDLOG_INFO("party{} start three party psi", self_rank);
    ThreePartyPsi(inputs, intersection);
  } else {
    SPDLOG_INFO("party{} start multi party psi", self_rank);
    MultiPartyPsi(inputs, intersection);
  }
  return intersection;
}

void NtyMPsi::MultiPartyPsi(const std::vector<uint128_t>& inputs,
                            std::vector<uint128_t>& intersection) {
  auto [ctx, wsize, self_rank] = CollectContext();
  auto counts = AllGatherItemsSize(ctx, inputs.size());
  // It is recommended that the party with smaller set be designated as
  // p0 and pi(i > 2), the party with larger set be designated as p1 and p2.
  if (self_rank == options_.aided_server_rank) {
    // generate okvs and prf key
    std::vector<uint128_t> seeds;
    for (uint64_t i = 0; i < wsize; ++i) {
      if (i == options_.aided_server_rank || i == options_.helper_rank ||
          i == options_.receiver_rank) {
        continue;
      }
      uint128_t seed = yacl::crypto::RandU128();
      seeds.push_back(seed);
      yacl::ByteContainerView seed_buf(&seed, sizeof(uint128_t));
      p2p_[i]->SendAsyncThrottled(p2p_[i]->NextRank(), seed_buf,
                                  fmt::format("send seed_buf"));
    }
    std::vector<uint128_t> hashed_items(inputs.size());
    std::vector<uint128_t> tmp(inputs.size());
    for (const auto& seed : seeds) {
      yacl::crypto::AES_KEY aes_key;
      AES_set_encrypt_key(seed, &aes_key);
      AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(inputs),
                           absl::MakeSpan(tmp));
      for (uint64_t i = 0; i < inputs.size(); ++i) {
        hashed_items[i] ^= tmp[i];
      }
    }

    GenOkvs(inputs, hashed_items, counts[self_rank],
            p2p_[options_.receiver_rank]);
    SPDLOG_DEBUG("aided server send okvs result to receiver success");
    // act as server-aided two party psi server
    ServerAidedTwoPartyPsi::ServerAssist(options_.helper_rank,
                                         options_.receiver_rank, p2p_);
    SPDLOG_INFO("aided server finished server-aided two party psi");

    if (options_.broadcast_result) {
      auto buffer = yacl::link::Broadcast(
          options_.link_ctx, {}, options_.receiver_rank, "broadcast result");
      intersection.resize(buffer.size() / sizeof(uint128_t));
      std::memcpy(intersection.data(), buffer.data<uint8_t>(), buffer.size());
    }
  } else if (self_rank == options_.helper_rank) {
    // decode okvs and xor in multiparty settings
    std::vector<std::vector<uint128_t>> recv_hashed_items(wsize);
    std::vector<uint128_t> hashed_items(inputs.size());
    auto recv_okvs = [&](size_t id) {
      recv_hashed_items[id].resize(inputs.size());
      DecodeOkvs(inputs, counts[id], p2p_[id], recv_hashed_items[id]);
    };
    std::vector<std::future<void>> futures;
    for (uint64_t i = 0; i < wsize; ++i) {
      if (i != options_.aided_server_rank && i != options_.helper_rank &&
          i != options_.receiver_rank) {
        futures.push_back(
            std::async(std::launch::async, std::ref(recv_okvs), i));
      }
    }
    for (auto& f : futures) {
      f.get();
    }
    SPDLOG_DEBUG("helper recv and decode okvs success");
    for (uint64_t i = 0; i < wsize; ++i) {
      if (i != options_.aided_server_rank && i != options_.helper_rank &&
          i != options_.receiver_rank) {
        for (uint64_t j = 0; j < inputs.size(); ++j)
          hashed_items[j] ^= recv_hashed_items[i][j];
      }
    }

    // avoid collisions in okvs decoding,
    // which is not necessary if okvs is bug free
    std::transform(inputs.begin(), inputs.end(), hashed_items.begin(),
                   hashed_items.begin(),
                   [](uint128_t a, uint128_t b) { return a ^ b; });
    // two party psi with server-aided
    std::vector<uint128_t> raw_intersection_result;
    ServerAidedTwoPartyPsi::ServerAidedPsi(
        hashed_items, self_rank, options_.aided_server_rank,
        options_.helper_rank, options_.receiver_rank, p2p_,
        raw_intersection_result);
    SPDLOG_INFO("helper finished server-aided two party psi");

    if (options_.broadcast_result) {
      auto buffer = yacl::link::Broadcast(
          options_.link_ctx, {}, options_.receiver_rank, "broadcast result");
      intersection.resize(buffer.size() / sizeof(uint128_t));
      std::memcpy(intersection.data(), buffer.data<uint8_t>(), buffer.size());
    }
  } else if (self_rank == options_.receiver_rank) {
    std::vector<uint128_t> recv_hashed_items(inputs.size());
    DecodeOkvs(inputs, counts[options_.aided_server_rank],
               p2p_[options_.aided_server_rank], recv_hashed_items);
    SPDLOG_DEBUG("receiver recv and decode okvs success");
    std::transform(inputs.begin(), inputs.end(), recv_hashed_items.begin(),
                   recv_hashed_items.begin(),
                   [](uint128_t a, uint128_t b) { return a ^ b; });
    // two party psi with server-aided
    std::vector<uint128_t> raw_intersection_result;
    ServerAidedTwoPartyPsi::ServerAidedPsi(
        recv_hashed_items, self_rank, options_.aided_server_rank,
        options_.helper_rank, options_.receiver_rank, p2p_,
        raw_intersection_result);
    GetIntersection(inputs, recv_hashed_items, raw_intersection_result,
                    intersection);
    SPDLOG_INFO("receiver get intersection results success");

    if (options_.broadcast_result) {
      auto buffer = yacl::Buffer(intersection.data(),
                                 intersection.size() * sizeof(uint128_t));
      yacl::link::Broadcast(options_.link_ctx, buffer, options_.receiver_rank,
                            "broadcast result");
    }
  } else {
    // compute prf and okvs
    uint128_t prf_seed;
    yacl::Buffer prf_seed_buf = p2p_[options_.aided_server_rank]->Recv(
        p2p_[options_.aided_server_rank]->NextRank(),
        fmt::format("recv prf seed"));
    YACL_ENFORCE(prf_seed_buf.size() == sizeof(uint128_t));
    std::memcpy(&prf_seed, prf_seed_buf.data(), prf_seed_buf.size());
    std::vector<uint128_t> hashed_items(inputs.size());
    yacl::crypto::AES_KEY aes_key;
    AES_set_encrypt_key(prf_seed, &aes_key);
    AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(inputs),
                         absl::MakeSpan(hashed_items));

    GenOkvs(inputs, hashed_items, counts[self_rank],
            p2p_[options_.helper_rank]);

    SPDLOG_INFO("party {} finished generating and sending okvs result",
                self_rank);

    if (options_.broadcast_result) {
      auto buffer = yacl::link::Broadcast(
          options_.link_ctx, {}, options_.receiver_rank, "broadcast result");
      intersection.resize(buffer.size() / sizeof(uint128_t));
      std::memcpy(intersection.data(), buffer.data<uint8_t>(), buffer.size());
    }
  }
}

void NtyMPsi::ThreePartyPsi(const std::vector<uint128_t>& inputs,
                            std::vector<uint128_t>& intersection) {
  auto [ctx, wsize, self_rank] = CollectContext();
  auto counts = AllGatherItemsSize(ctx, inputs.size());

  if (self_rank == options_.aided_server_rank) {
    // gen okvs and prf key
    uint128_t seed = yacl::crypto::RandU128();
    yacl::ByteContainerView seed_buf(&seed, sizeof(uint128_t));

    p2p_[options_.helper_rank]->SendAsyncThrottled(
        p2p_[options_.helper_rank]->NextRank(), seed_buf,
        fmt::format("send seed_buf"));

    std::vector<uint128_t> hashed_items(inputs.size());
    yacl::crypto::AES_KEY aes_key;
    AES_set_encrypt_key(seed, &aes_key);
    AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(inputs),
                         absl::MakeSpan(hashed_items));
    // gen okvs encode

    GenOkvs(inputs, hashed_items, counts[self_rank],
            p2p_[options_.receiver_rank]);
    ServerAidedTwoPartyPsi::ServerAssist(options_.helper_rank,
                                         options_.receiver_rank, p2p_);
    SPDLOG_INFO("aided server finished server-aided two party psi");

    if (options_.broadcast_result) {
      auto buffer = yacl::link::Broadcast(
          options_.link_ctx, {}, options_.receiver_rank, "broadcast result");
      intersection.resize(buffer.size() / sizeof(uint128_t));
      std::memcpy(intersection.data(), buffer.data<uint8_t>(), buffer.size());
    }
  } else if (self_rank == options_.helper_rank) {
    uint128_t prf_seed;
    yacl::Buffer prf_seed_buf = p2p_[options_.aided_server_rank]->Recv(
        p2p_[options_.aided_server_rank]->NextRank(),
        fmt::format("recv prf seed"));
    YACL_ENFORCE(prf_seed_buf.size() == sizeof(uint128_t));
    std::memcpy(&prf_seed, prf_seed_buf.data(), prf_seed_buf.size());
    // compute prf
    std::vector<uint128_t> hashed_items(inputs.size());
    yacl::crypto::AES_KEY aes_key;
    AES_set_encrypt_key(prf_seed, &aes_key);
    AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(inputs),
                         absl::MakeSpan(hashed_items));
    SPDLOG_DEBUG("helper get oprf result success");
    std::transform(inputs.begin(), inputs.end(), hashed_items.begin(),
                   hashed_items.begin(),
                   [](uint128_t a, uint128_t b) { return a ^ b; });
    std::vector<uint128_t> raw_intersection_result;
    ServerAidedTwoPartyPsi::ServerAidedPsi(
        hashed_items, self_rank, options_.aided_server_rank,
        options_.helper_rank, options_.receiver_rank, p2p_,
        raw_intersection_result);
    SPDLOG_INFO("helper finished server-aided two party psi");

    if (options_.broadcast_result) {
      auto buffer = yacl::link::Broadcast(
          options_.link_ctx, {}, options_.receiver_rank, "broadcast result");
      intersection.resize(buffer.size() / sizeof(uint128_t));
      std::memcpy(intersection.data(), buffer.data<uint8_t>(), buffer.size());
    }
  } else if (self_rank == options_.receiver_rank) {
    std::vector<uint128_t> recv_hashed_items(inputs.size());
    DecodeOkvs(inputs, counts[options_.aided_server_rank],
               p2p_[options_.aided_server_rank], recv_hashed_items);
    SPDLOG_DEBUG("receiver recv and decode okvs success");
    std::transform(inputs.begin(), inputs.end(), recv_hashed_items.begin(),
                   recv_hashed_items.begin(),
                   [](uint128_t a, uint128_t b) { return a ^ b; });
    // 2psi with server aid
    std::vector<uint128_t> raw_intersection_result;
    ServerAidedTwoPartyPsi::ServerAidedPsi(
        recv_hashed_items, self_rank, options_.aided_server_rank,
        options_.helper_rank, options_.receiver_rank, p2p_,
        raw_intersection_result);
    GetIntersection(inputs, recv_hashed_items, raw_intersection_result,
                    intersection);
    SPDLOG_INFO("receiver get intersection results success");

    if (options_.broadcast_result) {
      auto buffer = yacl::Buffer(intersection.data(),
                                 intersection.size() * sizeof(uint128_t));
      yacl::link::Broadcast(options_.link_ctx, buffer, options_.receiver_rank,
                            "broadcast result");
    }
  } else {
    SPDLOG_ERROR("input wrong party number");
  }
}

void NtyMPsi::GenOkvs(const std::vector<uint128_t>& items,
                      std::vector<uint128_t>& hashed_items,
                      const size_t& okvs_size,
                      const std::shared_ptr<yacl::link::Context>& ctx) {
  // gen okvs encode
  uint128_t baxos_seed = yacl::crypto::RandU128();
  psi::rr22::okvs::Baxos baxos;
  baxos.Init(okvs_size, bin_size_, paxos_weight_, kDefaultSSP,
             psi::rr22::okvs::PaxosParam::DenseType::GF128, baxos_seed);
  yacl::ByteContainerView paxos_seed_buf(&baxos_seed, sizeof(uint128_t));
  ctx->SendAsyncThrottled(ctx->NextRank(), paxos_seed_buf,
                          fmt::format("send baxos_seed_buf"));

  std::vector<uint128_t> p128_v(baxos.size(), 0);
  auto p128_span = absl::MakeSpan(p128_v);
  auto hashed_span = absl::MakeSpan(hashed_items);
  baxos.Solve(absl::MakeSpan(items), hashed_span, p128_span, nullptr,
              options_.num_threads);
  psi::rr22::SendChunked(ctx, p128_span);
}

void NtyMPsi::DecodeOkvs(const std::vector<uint128_t>& items,
                         const size_t& okvs_size,
                         const std::shared_ptr<yacl::link::Context>& ctx,
                         std::vector<uint128_t>& recv_hashed_items) {
  // decode okvs
  psi::rr22::okvs::Baxos baxos;
  // recv seed from p2~pn-2
  uint128_t baxos_seed;
  yacl::Buffer baxos_seed_buf =
      ctx->Recv(ctx->NextRank(), fmt::format("recv baxos seed"));
  YACL_ENFORCE(baxos_seed_buf.size() == sizeof(uint128_t));
  std::memcpy(&baxos_seed, baxos_seed_buf.data(), baxos_seed_buf.size());
  baxos.Init(okvs_size, bin_size_, paxos_weight_, kDefaultSSP,
             psi::rr22::okvs::PaxosParam::DenseType::GF128, baxos_seed);

  uint64_t baxos_size_ = baxos.size();
  auto p_solve = psi::rr22::RecvChunked<uint128_t>(ctx, baxos_size_);
  absl::Span<uint128_t> p128_span = absl::MakeSpan(
      reinterpret_cast<uint128_t*>(p_solve.data()), baxos.size());
  baxos.Decode(absl::MakeConstSpan(items), absl::MakeSpan(recv_hashed_items),
               p128_span, options_.num_threads);
}

void NtyMPsi::GetIntersection(const std::vector<uint128_t>& inputs,
                              const std::vector<uint128_t>& hashed_items,
                              const std::vector<uint128_t>& raw_result,
                              std::vector<uint128_t>& intersection) {
  google::dense_hash_map<uint128_t, uint32_t, NoHash> map(hashed_items.size());
  map.set_empty_key(yacl::crypto::RandU128());
  std::vector<uint32_t> intersection_idx;
  for (uint64_t i = 0; i < hashed_items.size(); ++i) {
    map.insert({hashed_items[i], i});
  }
  for (const auto& inter : raw_result) {
    auto iter = map.find(inter);
    if (iter != map.end()) {
      intersection_idx.push_back(iter->second);
    }
  }

  for (uint64_t i = 0; i < intersection_idx.size(); ++i) {
    intersection.emplace_back(inputs[intersection_idx[i]]);
  }
}

}  // namespace psi::nty
