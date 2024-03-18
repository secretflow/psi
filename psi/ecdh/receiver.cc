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

#include "psi/ecdh/receiver.h"

#include <filesystem>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "yacl/base/exception.h"
#include "yacl/utils/scope_guard.h"

#include "psi/cryptor/cryptor_selector.h"
#include "psi/ecdh/common.h"
#include "psi/trace_categories.h"
#include "psi/utils/sync.h"

#include "psi/proto/psi.pb.h"

namespace psi::ecdh {

EcdhPsiReceiver::EcdhPsiReceiver(const v2::PsiConfig &config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiReceiver(config, std::move(lctx)) {
  trunc_intersection_indices_ = true;
}

void EcdhPsiReceiver::Init() {
  TRACE_EVENT("init", "EcdhPsiReceiver::Init");
  SPDLOG_INFO("[EcdhPsiReceiver::Init] start");

  AbstractPsiReceiver::Init();

  if (recovery_manager_) {
    recovery_manager_->MarkInitEnd(config_, key_hash_digest_);
  }

  SPDLOG_INFO("[EcdhPsiReceiver::Init] end");
}

void EcdhPsiReceiver::PreProcess() {
  TRACE_EVENT("pre-process", "EcdhPsiReceiver::PreProcess");
  SPDLOG_INFO("[EcdhPsiReceiver::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  psi_options_.ecc_cryptor =
      CreateEccCryptor(config_.protocol_config().ecdh_config().curve());
  psi_options_.link_ctx = lctx_;

  // NOTE(junfeng): Only difference between receiver and sender.
  psi_options_.target_rank = lctx_->Rank();
  if (config_.protocol_config().broadcast_result()) {
    psi_options_.target_rank = yacl::link::kAllRank;
  }

  psi_options_.ic_mode = false;

  batch_provider_ = std::make_shared<ArrowCsvBatchProvider>(
      config_.input_config().path(), selected_keys_);

  if (recovery_manager_) {
    self_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        recovery_manager_->ecdh_dual_masked_self_cache_path(), kDefaultBinNum,
        false);
    peer_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        recovery_manager_->ecdh_dual_masked_peer_cache_path(), kDefaultBinNum,
        false);
    recovery_manager_->MarkPreProcessEnd(psi_options_.ecc_cryptor);
    psi_options_.recovery_manager = recovery_manager_;
  } else {
    self_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        std::filesystem::temp_directory_path(), kDefaultBinNum);
    peer_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        std::filesystem::temp_directory_path(), kDefaultBinNum);
  }

  SPDLOG_INFO("[EcdhPsiReceiver::PreProcess] end");
}

void EcdhPsiReceiver::Online() {
  TRACE_EVENT("online", "EcdhPsiReceiver::Online");
  SPDLOG_INFO("[EcdhPsiReceiver::Online] start");

  if (digest_equal_) {
    return;
  }

  bool online_stage_finished =
      recovery_manager_ ? recovery_manager_->MarkOnlineStart(lctx_) : false;

  if (!online_stage_finished) {
    auto run_f = std::async([&] {
      return RunEcdhPsi(psi_options_, batch_provider_, self_ec_point_store_,
                        peer_ec_point_store_);
    });

    SyncWait(lctx_, &run_f);
  }

  report_.set_original_count(batch_provider_->row_cnt());

  if (recovery_manager_) {
    recovery_manager_->MarkOnlineEnd();
  }

  SPDLOG_INFO("[EcdhPsiReceiver::Online] end");
}

void EcdhPsiReceiver::PostProcess() {
  TRACE_EVENT("post-process", "EcdhPsiReceiver::PostProcess");
  SPDLOG_INFO("[EcdhPsiReceiver::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  auto compute_indices_f = std::async([&] {
    FinalizeAndComputeIndices(self_ec_point_store_, peer_ec_point_store_,
                              intersection_indices_writer_.get());
  });

  SyncWait(lctx_, &compute_indices_f);

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[EcdhPsiReceiver::PostProcess] end");
}

}  // namespace psi::ecdh
