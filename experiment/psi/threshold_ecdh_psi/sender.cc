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

#include "experiment/psi/threshold_ecdh_psi/sender.h"

#include "experiment/psi/threshold_ecdh_psi/common.h"
#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi.h"

#include "psi/trace_categories.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {
ThresholdEcdhPsiSender::ThresholdEcdhPsiSender(
    const v2::PsiConfig& config, std::shared_ptr<yacl::link::Context> lctx)
    : EcdhPsiSender(config, std::move(lctx)) {
  threshold_ = config.intersection_threshold();
  index_link_ctx_ = lctx_->Spawn();
}

void ThresholdEcdhPsiSender::Online() {
  TRACE_EVENT("online", "ThresholdEcdhPsiSender::Online");
  SPDLOG_INFO("[ThresholdEcdhPsiSender::Online] start");

  if (digest_equal_) {
    return;
  }
  bool online_stage_finished =
      recovery_manager_ ? recovery_manager_->MarkOnlineStart(lctx_) : false;

  if (!online_stage_finished) {
    SyncWait(lctx_, [&] {
      auto cache_path = GetTaskDir();
      RunSender(psi_options_, cache_path, batch_provider_, self_ec_point_store_,
                peer_ec_point_store_);
    });
  }

  if (recovery_manager_) {
    recovery_manager_->MarkOnlineEnd();
  }

  SPDLOG_INFO("[ThresholdEcdhPsiSender::Online] end");
}

void ThresholdEcdhPsiSender::PostProcess() {
  TRACE_EVENT("post-process", "ThresholdEcdhPsiSender::PostProcess");
  SPDLOG_INFO("[ThresholdEcdhPsiSender::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  SyncWait(lctx_, [&] {
    auto cache_path = GetTaskDir();
    std::shared_ptr<ThresholdEcdhPsiCacheProvider> cache_provider =
        std::make_shared<ThresholdEcdhPsiCacheProvider>(
            cache_path, psi_options_.batch_size);

    RestoreAndSendIndex(cache_provider, intersection_indices_writer_);
  });

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[ThresholdEcdhPsiSender::PostProcess] end");
}

void ThresholdEcdhPsiSender::RestoreAndSendIndex(
    const std::shared_ptr<ThresholdEcdhPsiCacheProvider>& cache_provider,
    const std::shared_ptr<IndexWriter>& index_writer) {
  std::vector<uint32_t> peer_shuffled_index;
  std::vector<uint32_t> self_index;

  uint32_t final_intersection_count = 0;
  uint32_t real_intersection_count = 0;
  uint32_t target_intersection_count = 0;

  // Sender recv receiver's shuffled index. If sender can touch results,
  // sender recv the intersection indexes.
  std::tie(peer_shuffled_index, real_intersection_count) =
      RecvShuffledIndexAndCount();

  final_intersection_count = peer_shuffled_index.size();
  target_intersection_count = std::min(real_intersection_count, threshold_);
  YACL_ENFORCE(final_intersection_count == target_intersection_count,
               "The threshold has not taken effect, target size is {}, "
               "but the final size is {}",
               target_intersection_count, final_intersection_count);

  if (config_.protocol_config().broadcast_result()) {
    self_index = RecvSelfIndex();
  }

  std::vector<uint32_t> peer_restored_index(final_intersection_count);
  std::vector<uint32_t> peer_dup_cnt(final_intersection_count);

  std::unordered_map<uint32_t, uint32_t> shuffled_index_map;

  for (uint32_t i = 0; i != final_intersection_count; ++i) {
    shuffled_index_map[peer_shuffled_index[i]] = i;
  }

  // Sender restore receiver's shuffled indexes
  while (true) {
    auto shuffled_batch = cache_provider->ReadNextShuffledBatch();

    auto& index = shuffled_batch.batch_indices;
    auto& shuffled_index = shuffled_batch.shuffled_indices;
    auto& dup_cnt = shuffled_batch.dup_cnts;

    if (index.empty()) {
      break;
    }

    for (size_t i = 0; i != index.size(); ++i) {
      auto iter = shuffled_index_map.find(index[i]);
      if (iter == shuffled_index_map.end()) {
        continue;
      }
      peer_restored_index[iter->second] = shuffled_index[i];
      peer_dup_cnt[iter->second] = dup_cnt[i];
    }
  }

  // Sender send restored indexes to receiver.
  SendRestoredIndex(peer_restored_index);

  // If sender can touch results, sender store intersection index and receiver's
  // duplicate count.
  if (config_.protocol_config().broadcast_result()) {
    for (size_t i = 0; i < final_intersection_count; ++i) {
      index_writer->WriteCache(self_index[i], peer_dup_cnt[i]);
    }
    index_writer->Commit();

    if (!config_.count_path().empty()) {
      SaveIntersectionCount(config_.count_path(), real_intersection_count,
                            final_intersection_count);
    }
  }
}

void ThresholdEcdhPsiSender::SendRestoredIndex(
    const std::vector<uint32_t>& peer_restored_index) {
  index_link_ctx_->SendAsyncThrottled(
      index_link_ctx_->NextRank(), utils::SerializeIndexes(peer_restored_index),
      "send receiver's restored indexes");
  SPDLOG_INFO("Send receiver's restored indexes to sender");
}

std::pair<std::vector<uint32_t>, uint32_t>
ThresholdEcdhPsiSender::RecvShuffledIndexAndCount() {
  std::vector<uint32_t> peer_shuffled_index;
  uint32_t real_intersection_unique_key_count = 0;

  auto index_buffer = index_link_ctx_->Recv(index_link_ctx_->NextRank(),
                                            "recv receiver's shuffled indexes");
  peer_shuffled_index = utils::DeserializeIndexes(index_buffer);

  auto count_buffer = index_link_ctx_->Recv(index_link_ctx_->NextRank(),
                                            "recv real intersection count");
  YACL_ENFORCE(count_buffer.size() == sizeof(uint32_t));
  std::memcpy(&real_intersection_unique_key_count, count_buffer.data(),
              sizeof(uint32_t));
  SPDLOG_INFO("Recv shuffled indexes and real intersection count");

  return {peer_shuffled_index, real_intersection_unique_key_count};
}

std::vector<uint32_t> ThresholdEcdhPsiSender::RecvSelfIndex() {
  std::vector<uint32_t> self_index;

  auto buffer = index_link_ctx_->Recv(index_link_ctx_->NextRank(),
                                      "recv sender's indexes");
  self_index = utils::DeserializeIndexes(buffer);
  SPDLOG_INFO("Recv self intersection indexes");

  return self_index;
}
}  // namespace psi::ecdh