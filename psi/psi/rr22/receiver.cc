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

#include "psi/psi/rr22/receiver.h"

#include "yacl/crypto/base/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/psi/bucket.h"
#include "psi/psi/bucket_psi.h"
#include "psi/psi/prelude.h"
#include "psi/psi/rr22/common.h"
#include "psi/psi/trace_categories.h"
#include "psi/psi/utils/serialize.h"
#include "psi/psi/utils/utils.h"

namespace psi::psi::rr22 {

Rr22PSIReceiver::Rr22PSIReceiver(const v2::PsiConfig &config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPSIReceiver(config, std::move(lctx)) {}

void Rr22PSIReceiver::Init() {
  TRACE_EVENT("init", "Rr22PSIReceiver::Init");
  SPDLOG_INFO("[Rr22PSIReceiver::Init] start");

  AbstractPSIReceiver::Init();

  CommonInit(key_hash_digest_, &config_, recovery_manager_.get());
  SPDLOG_INFO("[Rr22PSIReceiver::Init] end");
}

void Rr22PSIReceiver::PreProcess() {
  TRACE_EVENT("pre-process", "Rr22PSIReceiver::PreProcess");
  SPDLOG_INFO("[Rr22PSIReceiver::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  bucket_count_ =
      NegotiateBucketNum(lctx_, report_.original_count(),
                         config_.protocol_config().rr22_config().bucket_size(),
                         config_.protocol_config().protocol());

  parallelism_ = NegotiateParallelismNum(
      lctx_, config_.protocol_config().rr22_config().parallelism(),
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

  SPDLOG_INFO("[Rr22PSIReceiver::PreProcess] end");
}

void Rr22PSIReceiver::Online() {
  TRACE_EVENT("online", "Rr22PSIReceiver::Online");
  SPDLOG_INFO("[Rr22PSIReceiver::Online] start");

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

  if (recovery_manager_ == nullptr)
    intersection_indices_writer_->InitBucketWrite(
        input_bucket_store_->BucketNum());
  PsiParallelFor(
      bucket_idx, input_bucket_store_->BucketNum(),
      recovery_manager_ ? 1 : parallelism_, [&](size_t bucket_idx) {
        auto bucket_items_list =
            PrepareBucketData(config_.protocol_config().protocol(), bucket_idx,
                              lctx_, input_bucket_store_.get());

        if (!bucket_items_list.has_value()) {
          return;
        }

        auto run_f = std::async([&] {
          std::vector<HashBucketCache::BucketItem> res;

          std::vector<uint128_t> items_hash(bucket_items_list->size());
          yacl::parallel_for(0, bucket_items_list->size(),
                             [&](int64_t begin, int64_t end) {
                               for (int64_t i = begin; i < end; ++i) {
                                 items_hash[i] = yacl::crypto::Blake3_128(
                                     bucket_items_list->at(i).base64_data);
                               }
                             });

          std::vector<size_t> rr22_psi_result =
              Rr22PsiReceiver(rr22_options, lctx_, items_hash);
          res.reserve(rr22_psi_result.size());

          for (auto index : rr22_psi_result) {
            res.emplace_back(bucket_items_list->at(index));
          }
          return res;
        });

        std::vector<HashBucketCache::BucketItem> result_list =
            SyncWait(lctx_, &run_f);

        auto write_bucket_res_f = std::async([&] {
          HandleBucketResultByReceiver(
              config_.protocol_config().broadcast_result(), lctx_, result_list,
              intersection_indices_writer_.get(),
              recovery_manager_ == nullptr ? std::optional<size_t>(bucket_idx)
                                           : std::nullopt);
        });

        SyncWait(lctx_, &write_bucket_res_f);

        if (recovery_manager_) {
          recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
        }
      });
  if (recovery_manager_ == nullptr)
    intersection_indices_writer_->WaitBucketWriteDone();

  SPDLOG_INFO("[Rr22PSIReceiver::Online] end");
}

void Rr22PSIReceiver::PostProcess() {
  TRACE_EVENT("post-process", "Rr22PSIReceiver::PostProcess");
  SPDLOG_INFO("[Rr22PSIReceiver::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[Rr22PSIReceiver::PostProcess] end");
}

}  // namespace psi::psi::rr22
