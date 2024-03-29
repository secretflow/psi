// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/rr22/receiver.h"

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/parallel.h"

#include "psi/legacy/bucket_psi.h"
#include "psi/prelude.h"
#include "psi/rr22/common.h"
#include "psi/trace_categories.h"
#include "psi/utils/bucket.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::rr22 {

Rr22PsiReceiver::Rr22PsiReceiver(const v2::PsiConfig &config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiReceiver(config, std::move(lctx)) {}

void Rr22PsiReceiver::Init() {
  TRACE_EVENT("init", "Rr22PsiReceiver::Init");
  SPDLOG_INFO("[Rr22PsiReceiver::Init] start");

  AbstractPsiReceiver::Init();

  CommonInit(key_hash_digest_, &config_, recovery_manager_.get());
  SPDLOG_INFO("[Rr22PsiReceiver::Init] end");
}

void Rr22PsiReceiver::PreProcess() {
  TRACE_EVENT("pre-process", "Rr22PsiReceiver::PreProcess");
  SPDLOG_INFO("[Rr22PsiReceiver::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  bucket_count_ =
      NegotiateBucketNum(lctx_, report_.original_count(),
                         config_.protocol_config().rr22_config().bucket_size(),
                         config_.protocol_config().protocol());

  if (bucket_count_ > 0) {
    std::vector<std::string> keys(config_.keys().begin(), config_.keys().end());

    auto gen_input_bucket_f = std::async([&] {
      if (recovery_manager_) {
        input_bucket_store_ = CreateCacheFromCsv(
            config_.input_config().path(), keys,
            recovery_manager_->input_bucket_store_path(), bucket_count_);
      } else {
        input_bucket_store_ = CreateCacheFromCsv(
            config_.input_config().path(), keys,
            std::filesystem::path(config_.input_config().path()).parent_path(),
            bucket_count_);
      }
    });

    SyncWait(lctx_, &gen_input_bucket_f);
  }

  if (recovery_manager_) {
    recovery_manager_->MarkPreProcessEnd();
  }

  SPDLOG_INFO("[Rr22PsiReceiver::PreProcess] end");
}

void Rr22PsiReceiver::Online() {
  TRACE_EVENT("online", "Rr22PsiReceiver::Online");
  SPDLOG_INFO("[Rr22PsiReceiver::Online] start");

  if (digest_equal_) {
    return;
  }

  if (bucket_count_ == 0) {
    return;
  }

  bool online_stage_finished =
      recovery_manager_ ? recovery_manager_->MarkOnlineStart(lctx_) : false;

  if (online_stage_finished) {
    return;
  }

  size_t bucket_idx =
      recovery_manager_
          ? std::min(recovery_manager_->parsed_bucket_count_from_peer(),
                     recovery_manager_->checkpoint().parsed_bucket_count())
          : 0;

  Rr22PsiOptions rr22_options = GenerateRr22PsiOptions(
      config_.protocol_config().rr22_config().low_comm_mode());

  for (; bucket_idx < input_bucket_store_->BucketNum(); bucket_idx++) {
    auto bucket_items_list =
        PrepareBucketData(config_.protocol_config().protocol(), bucket_idx,
                          lctx_, input_bucket_store_.get());

    if (!bucket_items_list.has_value()) {
      continue;
    }

    auto run_f = std::async([&] {
      std::vector<HashBucketCache::BucketItem> res;

      // Gather Items Size
      std::vector<size_t> items_size =
          AllGatherItemsSize(lctx_, bucket_items_list->size());
      size_t max_size =
          std::max(items_size[lctx_->Rank()], items_size[lctx_->NextRank()]);

      std::vector<uint128_t> items_hash(bucket_items_list->size());
      yacl::parallel_for(0, bucket_items_list->size(),
                         [&](int64_t begin, int64_t end) {
                           for (int64_t i = begin; i < end; ++i) {
                             items_hash[i] = yacl::crypto::Blake3_128(
                                 bucket_items_list->at(i).base64_data);
                           }
                         });
      if (bucket_items_list->size() < max_size) {
        items_hash.resize(max_size);
        for (size_t idx = bucket_items_list->size(); idx < max_size; idx++) {
          items_hash[idx] = yacl::crypto::SecureRandU128();
        }
      }

      std::vector<size_t> rr22_psi_result =
          Rr22PsiReceiverInternal(rr22_options, lctx_, items_hash);
      res.reserve(rr22_psi_result.size());

      for (auto index : rr22_psi_result) {
        res.emplace_back(bucket_items_list->at(index));
      }
      return res;
    });

    std::vector<HashBucketCache::BucketItem> result_list =
        SyncWait(lctx_, &run_f);

    auto write_bucket_res_f = std::async([&] {
      HandleBucketResultByReceiver(config_.protocol_config().broadcast_result(),
                                   lctx_, result_list,
                                   intersection_indices_writer_.get());
    });

    SyncWait(lctx_, &write_bucket_res_f);

    if (recovery_manager_) {
      recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
    }
  }

  SPDLOG_INFO("[Rr22PsiReceiver::Online] end");
}

void Rr22PsiReceiver::PostProcess() {
  TRACE_EVENT("post-process", "Rr22PsiReceiver::PostProcess");
  SPDLOG_INFO("[Rr22PsiReceiver::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[Rr22PsiReceiver::PostProcess] end");
}

}  // namespace psi::rr22
