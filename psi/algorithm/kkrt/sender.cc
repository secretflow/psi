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

#include "psi/algorithm/kkrt/sender.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/kkrt/common.h"
#include "psi/algorithm/kkrt/kkrt_psi.h"
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

  SPDLOG_INFO("[KkrtPsiSender::Init] end");
}

void KkrtPsiSender::PreProcess() {
  TRACE_EVENT("pre-process", "KkrtPsiSender::PreProcess");
  SPDLOG_INFO("[KkrtPsiSender::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  bucket_count_ =
      NegotiateBucketNum(lctx_, report_.original_key_count(),
                         config_.protocol_config().kkrt_config().bucket_size(),
                         config_.protocol_config().protocol());

  if (bucket_count_ > 0) {
    std::vector<std::string> keys(config_.keys().begin(), config_.keys().end());

    SyncWait(lctx_, [&] {
      if (recovery_manager_) {
        input_bucket_store_ = CreateCacheFromProvider(
            batch_provider_, recovery_manager_->input_bucket_store_path(),
            bucket_count_);
      } else {
        input_bucket_store_ = CreateCacheFromProvider(
            batch_provider_, GetTaskDir() / "input_bucket_store",
            bucket_count_);
      }
    });
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
    // TODO(huocun): optimize bucket strore, cat use struct, no need serialize &
    // deserialize
    auto bucket_items_list =
        PrepareBucketData(config_.protocol_config().protocol(), bucket_idx,
                          lctx_, input_bucket_store_.get());
    if (!bucket_items_list.has_value()) {
      continue;
    }

    auto& bucket_items = *bucket_items_list;

    SyncWait(lctx_, [&] {
      CalcBucketItemSecHash(bucket_items);

      KkrtPsiSend(lctx_, *ot_recv_, bucket_items);
    });

    SyncWait(lctx_, [&] {
      HandleBucketResultBySender(config_.protocol_config().broadcast_result(),
                                 lctx_, bucket_items,
                                 intersection_indices_writer_.get());
    });

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
