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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/parallel.h"

#include "psi/legacy/bucket_psi.h"
#include "psi/prelude.h"
#include "psi/rr22/common.h"
#include "psi/rr22/rr22_psi.h"
#include "psi/rr22/rr22_utils.h"
#include "psi/trace_categories.h"
#include "psi/utils/bucket.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::rr22 {

Rr22PsiReceiver::Rr22PsiReceiver(const v2::PsiConfig& config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiReceiver(config, std::move(lctx)) {}

void Rr22PsiReceiver::Init() {
  TRACE_EVENT("init", "Rr22PsiReceiver::Init");
  SPDLOG_INFO("[Rr22PsiReceiver::Init] start");
  YACL_ENFORCE(lctx_->WorldSize() == 2);
  AbstractPsiReceiver::Init();

  SPDLOG_INFO("[Rr22PsiReceiver::Init] end");
}

void Rr22PsiReceiver::PreProcess() {
  TRACE_EVENT("pre-process", "Rr22PsiReceiver::PreProcess");
  SPDLOG_INFO("[Rr22PsiReceiver::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  bucket_count_ =
      NegotiateBucketNum(lctx_, report_.original_key_count(),
                         config_.protocol_config().rr22_config().bucket_size(),
                         config_.protocol_config().protocol());

  if (bucket_count_ > 0) {
    std::vector<std::string> keys(config_.keys().begin(), config_.keys().end());

    auto gen_input_bucket_f = std::async([&] {
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

  PreProcessFunc pre_f =
      [&](size_t idx) -> std::vector<HashBucketCache::BucketItem> {
    if (idx >= input_bucket_store_->BucketNum()) {
      return {};
    }
    return input_bucket_store_->LoadBucketItems(idx);
  };
  PostProcessFunc post_f =
      [&](size_t bucket_idx,
          const std::vector<HashBucketCache::BucketItem>& bucket_items,
          const std::vector<uint32_t>& indices,
          const std::vector<uint32_t>& peer_cnt) {
        for (size_t i = 0; i != indices.size(); ++i) {
          intersection_indices_writer_->WriteCache(
              bucket_items[indices[i]].index, peer_cnt[i]);
        }
        intersection_indices_writer_->Commit();
        if (recovery_manager_) {
          recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
        }
      };

  Rr22Runner runner(lctx_, rr22_options, input_bucket_store_->BucketNum(),
                    config_.protocol_config().broadcast_result(), pre_f,
                    post_f);
  auto f = std::async([&] { runner.AsyncRun(bucket_idx, false); });
  SyncWait(lctx_, &f);
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
