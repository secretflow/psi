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

#include "experiment/psi/threshold_ecdh_psi/receiver.h"

#include "experiment/psi/threshold_ecdh_psi/common.h"
#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi.h"

#include "psi/trace_categories.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {
ThresholdEcdhPsiReceiver::ThresholdEcdhPsiReceiver(
    const v2::PsiConfig& config, std::shared_ptr<yacl::link::Context> lctx)
    : EcdhPsiReceiver(config, std::move(lctx)) {
  threshold_ = config.intersection_threshold();
  index_link_ctx_ = lctx_->Spawn();
}

void ThresholdEcdhPsiReceiver::Online() {
  TRACE_EVENT("online", "ThresholdEcdhPsiSender::Online");
  SPDLOG_INFO("[ThresholdEcdhPsiSender::Online] start");

  if (digest_equal_) {
    return;
  }
  bool online_stage_finished =
      recovery_manager_ ? recovery_manager_->MarkOnlineStart(lctx_) : false;

  if (!online_stage_finished) {
    SyncWait(lctx_, [&] {
      RunReceiver(psi_options_, batch_provider_, self_ec_point_store_,
                  peer_ec_point_store_);
    });
  }

  if (recovery_manager_) {
    recovery_manager_->MarkOnlineEnd();
  }

  SPDLOG_INFO("[ThresholdEcdhPsiSender::Online] end");
}

void ThresholdEcdhPsiReceiver::PostProcess() {
  TRACE_EVENT("post-process", "ThresholdEcdhPsiReceiver::PostProcess");
  SPDLOG_INFO("[ThresholdEcdhPsiReceiver::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  SyncWait(lctx_, [&] {
    ComputeAndStoreIndex(self_ec_point_store_, peer_ec_point_store_,
                         intersection_indices_writer_);
  });

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[ThresholdEcdhPsiReceiver::PostProcess] end");
}

void ThresholdEcdhPsiReceiver::ComputeAndStoreIndex(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer,
    const std::shared_ptr<IndexWriter>& index_writer) {
  YACL_ENFORCE_EQ(self->num_bins(), peer->num_bins());
  self->Flush();
  peer->Flush();

  // We use `real_intersection_count` to represent the intersection unique key
  // count without threshold limitation, and `final_intersection_count` to
  // represent the intersection unique key count with threshold limitation.
  uint32_t real_intersection_count = 0;
  uint32_t final_intersection_count = 0;

  std::vector<uint32_t> self_shuffled_index;
  std::vector<uint32_t> peer_index;
  std::vector<uint32_t> peer_dup_cnt;

  // Receiver compute the intersection whose size does not exceed the threshold.
  for (size_t bin_idx = 0; bin_idx < self->num_bins(); ++bin_idx) {
    std::vector<HashBucketCache::BucketItem> self_results =
        self->LoadBucketItems(bin_idx);
    std::vector<HashBucketCache::BucketItem> peer_results =
        peer->LoadBucketItems(bin_idx);

    std::unordered_set<HashBucketCache::BucketItem,
                       HashBucketCache::HashBucketIter>
        peer_set(peer_results.begin(), peer_results.end());

    for (const auto& item : self_results) {
      auto peer = peer_set.find(item);
      if (peer != peer_set.end()) {
        real_intersection_count++;

        if (final_intersection_count < threshold_) {
          self_shuffled_index.emplace_back(item.index);
          peer_index.emplace_back(peer->index);
          peer_dup_cnt.emplace_back(peer->extra_dup_cnt);

          final_intersection_count++;
        } else {
          continue;
        }
      }
    }
  }

  SPDLOG_INFO("real intersection count: {}, final intersection count: {}",
              real_intersection_count, final_intersection_count);

  // Receiver send shuffled indexes to sender. If sender can touch results,
  // receiver send peer_index to sender.
  SendShuffledIndexAndCount(self_shuffled_index, real_intersection_count);
  if (config_.protocol_config().broadcast_result()) {
    SendPeerIndex(peer_index);
  }

  // Receiver recv restored indexes and store them by index_writer.
  std::vector<uint32_t> self_restored_index = RecvRestoredIndex();
  YACL_ENFORCE(
      final_intersection_count == self_restored_index.size(),
      "index size not match, target size is {}, but received size is {}",
      final_intersection_count, self_restored_index.size());

  for (size_t i = 0; i < final_intersection_count; ++i) {
    index_writer->WriteCache(self_restored_index[i], peer_dup_cnt[i]);
  }
  index_writer->Commit();

  if (!config_.count_path().empty()) {
    SaveIntersectionCount(config_.count_path(), real_intersection_count,
                          final_intersection_count);
  }
}

void ThresholdEcdhPsiReceiver::SendShuffledIndexAndCount(
    const std::vector<uint32_t>& self_shuffled_index,
    uint32_t real_intersection_unique_key_count) {
  index_link_ctx_->SendAsyncThrottled(
      index_link_ctx_->NextRank(), utils::SerializeIndexes(self_shuffled_index),
      "send shuffled indexes");
  index_link_ctx_->SendAsyncThrottled(
      index_link_ctx_->NextRank(),
      yacl::ByteContainerView(&real_intersection_unique_key_count,
                              sizeof(uint32_t)),
      "send real intersection count");
  SPDLOG_INFO("Send shuffled indexes and real intersection count to sender");
}

void ThresholdEcdhPsiReceiver::SendPeerIndex(
    const std::vector<uint32_t>& peer_index) {
  index_link_ctx_->SendAsyncThrottled(index_link_ctx_->NextRank(),
                                      utils::SerializeIndexes(peer_index),
                                      "send sender's indexes");
  SPDLOG_INFO("Send sender's intersection indexes to sender");
}

std::vector<uint32_t> ThresholdEcdhPsiReceiver::RecvRestoredIndex() {
  std::vector<uint32_t> self_restored_index;
  auto buffer = index_link_ctx_->Recv(index_link_ctx_->NextRank(),
                                      "recv restored indexes");
  self_restored_index = utils::DeserializeIndexes(buffer);
  SPDLOG_INFO("Recv restored indexes, size: {}", self_restored_index.size());

  return self_restored_index;
}

}  // namespace psi::ecdh