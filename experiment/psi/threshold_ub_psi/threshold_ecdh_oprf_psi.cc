// Copyright 2025
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

#include "experiment/psi/threshold_ub_psi/threshold_ecdh_oprf_psi.h"

#include <vector>

#include "yacl/crypto/rand/rand.h"

#include "psi/utils/serialize.h"

namespace psi::ecdh {
void ThresholdEcdhOprfPsiServer::RecvAndRestoreIndexes(
    uint32_t client_unique_count) {
  std::vector<uint32_t> client_indexes;
  auto buf =
      online_link_->Recv(online_link_->NextRank(), "client shuffled indexes");
  client_indexes = utils::DeserializeIndexes(buf);

  std::vector<uint32_t> shuffle_indexes(client_unique_count);
  std::iota(shuffle_indexes.begin(), shuffle_indexes.end(), 0);
  yacl::crypto::YaclReplayUrbg<uint32_t> gen(shuffle_seed_, shuffle_counter_);
  std::shuffle(shuffle_indexes.begin(), shuffle_indexes.end(), gen);

  // restore client's indexes
  for (size_t i = 0; i < client_indexes.size(); ++i) {
    client_indexes[i] = shuffle_indexes[client_indexes[i]];
  }

  // send client's restored indexes
  online_link_->SendAsyncThrottled(online_link_->NextRank(),
                                   utils::SerializeIndexes(client_indexes),
                                   "send restored indexes");
  SPDLOG_INFO("Send restored indexes finished");
}

uint32_t ThresholdEcdhOprfPsiServer::RecvIntersectionUniqueKeyCount() {
  uint32_t real_intersection_unique_key_count = 0;
  auto buf =
      online_link_->Recv(online_link_->NextRank(), "recv unique key count");
  YACL_ENFORCE(buf.size() == sizeof(uint32_t));
  std::memcpy(&real_intersection_unique_key_count, buf.data(),
              sizeof(uint32_t));

  return real_intersection_unique_key_count;
}

void ThresholdEcdhOprfPsiClient::SendShuffledIndexes(
    const std::vector<uint32_t>& self_indexes) {
  SPDLOG_INFO("Start SendShuffledIndexes");
  online_link_->SendAsyncThrottled(online_link_->NextRank(),
                                   utils::SerializeIndexes(self_indexes),
                                   "send shuffled indexes");
  SPDLOG_INFO("End SendShuffledIndexes, send {} indexes", self_indexes.size());
}

std::vector<uint32_t> ThresholdEcdhOprfPsiClient::RecvRestoredIndexes() {
  std::vector<uint32_t> restored_indexes;
  auto buf =
      online_link_->Recv(online_link_->NextRank(), "recv restored indexes");
  restored_indexes = utils::DeserializeIndexes(buf);
  SPDLOG_INFO("Recv restored indexes, size: {}", restored_indexes.size());

  return restored_indexes;
}

void ThresholdEcdhOprfPsiClient::SendIntersectionUniqueKeyCount(
    uint32_t real_intersection_unique_key_count) {
  SPDLOG_INFO("Start SendIntersectionUniqueKeyCount");
  online_link_->SendAsyncThrottled(
      online_link_->NextRank(),
      yacl::ByteContainerView(&real_intersection_unique_key_count,
                              sizeof(uint32_t)),
      "send unique key count");
  SPDLOG_INFO("End SendIntersectionUniqueKeyCount");
}
}  // namespace psi::ecdh