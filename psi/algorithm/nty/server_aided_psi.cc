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

#include "psi/algorithm/nty/server_aided_psi.h"

#include <algorithm>
#include <future>

#include "yacl/crypto/aes/aes_intrinsics.h"
#include "yacl/crypto/rand/rand.h"

namespace psi::nty {

void SendEncItems(const std::vector<uint128_t>& items,
                  const uint128_t& aes_seed,
                  const std::shared_ptr<yacl::link::Context>& ctx) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(aes_seed, &aes_key);
  std::vector<uint128_t> enc_items(items.size());
  AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(items),
                       absl::MakeSpan(enc_items));
  std::mt19937 g(yacl::crypto::SecureRandU64());
  std::shuffle(enc_items.begin(), enc_items.end(), g);
  yacl::ByteContainerView enc_items_buf(enc_items.data(),
                                        sizeof(uint128_t) * items.size());
  ctx->SendAsyncThrottled(ctx->NextRank(), enc_items_buf,
                          fmt::format("send enc items"));
}

void ServerAidedTwoPartyPsi::ServerAidedPsi(
    const std::vector<uint128_t>& items, size_t self_rank,
    size_t aided_server_rank, size_t helper_rank, size_t receiver_rank,
    const std::vector<std::shared_ptr<yacl::link::Context>>& p2p,
    std::vector<uint128_t>& outputs) {
  uint128_t aes_seed;
  if (self_rank == receiver_rank) {
    yacl::Buffer aes_seed_buf = p2p[helper_rank]->Recv(
        p2p[helper_rank]->NextRank(), fmt::format("recv aes seed"));
    YACL_ENFORCE(aes_seed_buf.size() == sizeof(uint128_t));
    std::memcpy(&aes_seed, aes_seed_buf.data(), aes_seed_buf.size());
    SendEncItems(items, aes_seed, p2p[aided_server_rank]);
    // p2 get raw intersection data
    yacl::Buffer recv_data =
        p2p[aided_server_rank]->Recv(p2p[aided_server_rank]->NextRank(),
                                     fmt::format("recv raw intersection data"));
    if (recv_data.size() % sizeof(uint128_t) != 0) {
      SPDLOG_ERROR("recv data size should be a multiple of {} bytes",
                   sizeof(uint128_t));
    }

    outputs.resize(recv_data.size() / sizeof(uint128_t));
    std::vector<uint128_t> recv_cipher(recv_data.size() / sizeof(uint128_t));
    std::memcpy(recv_cipher.data(), recv_data.data(), recv_data.size());

    // dec
    yacl::crypto::AES_KEY aes_key;
    AES_set_decrypt_key(aes_seed, &aes_key);
    AES_ecb_decrypt_blks(aes_key, absl::MakeConstSpan(recv_cipher),
                         absl::MakeSpan(outputs));
  } else {
    yacl::crypto::AES_KEY aes_key;
    aes_seed = yacl::crypto::RandU128();
    yacl::ByteContainerView seed_buf(&aes_seed, sizeof(uint128_t));
    p2p[receiver_rank]->SendAsyncThrottled(
        p2p[receiver_rank]->NextRank(), seed_buf, fmt::format("send seed_buf"));
    SendEncItems(items, aes_seed, p2p[aided_server_rank]);
  }
}

void ServerAidedTwoPartyPsi::ServerAssist(
    size_t helper_rank, size_t receiver_rank,
    const std::vector<std::shared_ptr<yacl::link::Context>>& p2p) {
  std::vector<uint128_t> helper_prf, receiver_prf;
  auto recv_prf = [&](std::vector<uint128_t>& data, uint64_t id) {
    yacl::Buffer recv_data =
        p2p[id]->Recv(p2p[id]->NextRank(), fmt::format("recv data"));
    if (recv_data.size() % sizeof(uint128_t) != 0) {
      SPDLOG_ERROR("recv data size should be a multiple of {} bytes",
                   sizeof(uint128_t));
    }
    data.resize(recv_data.size() / sizeof(uint128_t));
    std::memcpy(data.data(), recv_data.data(), recv_data.size());
    std::sort(data.begin(), data.end());
  };
  auto future0 = std::async(std::launch::async, std::ref(recv_prf),
                            std::ref(helper_prf), helper_rank);
  auto future1 = std::async(std::launch::async, std::ref(recv_prf),
                            std::ref(receiver_prf), receiver_rank);
  future0.get();
  future1.get();

  // compute intersection
  std::vector<uint128_t> res;
  std::set_intersection(helper_prf.begin(), helper_prf.end(),
                        receiver_prf.begin(), receiver_prf.end(),
                        std::back_inserter(res));
  yacl::ByteContainerView res_buf(res.data(), sizeof(uint128_t) * res.size());
  auto send_result = [&](yacl::ByteContainerView& res_buf, uint64_t id) {
    p2p[id]->SendAsyncThrottled(p2p[id]->NextRank(), res_buf,
                                fmt::format("send result"));
  };
  auto send_future = std::async(std::launch::async, std::ref(send_result),
                                std::ref(res_buf), receiver_rank);
  send_future.get();
  SPDLOG_INFO("send intersection reuslt to receiver");
}

}  // namespace psi::nty
