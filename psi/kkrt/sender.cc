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

#include "psi/kkrt/sender.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/kkrt/common.h"
#include "psi/kkrt/kkrt_psi.h"
#include "psi/legacy/bucket_psi.h"
#include "psi/prelude.h"
#include "psi/trace_categories.h"
#include "psi/utils/bucket.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::kkrt {

KkrtPsiSender::KkrtPsiSender(const v2::PsiConfig& config,
                             std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiSender(config, std::move(lctx)) {}

void KkrtPsiSender::Init() {
  TRACE_EVENT("init", "KkrtPsiSender::Init");
  SPDLOG_INFO("[KkrtPsiSender::Init] start");

  AbstractPsiSender::Init();

  CommonInit(key_hash_digest_, &config_, recovery_manager_.get());
  SPDLOG_INFO("[KkrtPsiSender::Init] end");
}

void KkrtPsiSender::PreProcess() {
  TRACE_EVENT("pre-process", "KkrtPsiSender::PreProcess");
  SPDLOG_INFO("[KkrtPsiSender::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  bucket_count_ =
      NegotiateBucketNum(lctx_, report_.original_count(),
                         config_.protocol_config().kkrt_config().bucket_size(),
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

  if (bucket_count_ > 0) {
    ot_recv_ = std::make_unique<yacl::crypto::OtRecvStore>(
        GetKkrtOtSenderOptions(lctx_, kDefaultNumOt));
  }

  if (recovery_manager_) {
    recovery_manager_->MarkPreProcessEnd();
  }

  SPDLOG_INFO("[KkrtPsiSender::PreProcess] end");
}

void KkrtPsiSender::Online() {
  TRACE_EVENT("online", "KkrtPsiSender::Online");
  SPDLOG_INFO("[KkrtPsiSender::Online] start");

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

  for (; bucket_idx < input_bucket_store_->BucketNum(); bucket_idx++) {
    auto bucket_items_list =
        PrepareBucketData(config_.protocol_config().protocol(), bucket_idx,
                          lctx_, input_bucket_store_.get());

    if (!bucket_items_list.has_value()) {
      continue;
    }

    auto run_f = std::async([&] {
      std::vector<uint128_t> items_hash(bucket_items_list->size());
      yacl::parallel_for(0, bucket_items_list->size(),
                         [&](int64_t begin, int64_t end) {
                           for (int64_t i = begin; i < end; ++i) {
                             items_hash[i] = yacl::crypto::Blake3_128(
                                 bucket_items_list->at(i).base64_data);
                           }
                         });

      KkrtPsiSend(lctx_, *ot_recv_, items_hash);
    });

    SyncWait(lctx_, &run_f);

    auto write_bucket_res_f = std::async([&] {
      HandleBucketResultBySender(config_.protocol_config().broadcast_result(),
                                 lctx_, *bucket_items_list,
                                 intersection_indices_writer_.get());
    });

    SyncWait(lctx_, &write_bucket_res_f);

    if (recovery_manager_) {
      recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
    }
  }

  SPDLOG_INFO("[KkrtPsiSender::Online] end");
}

void KkrtPsiSender::PostProcess() {
  TRACE_EVENT("post-process", "KkrtPsiSender::PostProcess");
  SPDLOG_INFO("[KkrtPsiSender::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[KkrtPsiSender::PostProcess] end");
}

}  // namespace psi::kkrt
