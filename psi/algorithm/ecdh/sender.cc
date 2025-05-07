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

#include "psi/algorithm/ecdh/sender.h"

#include <filesystem>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/utils/scope_guard.h"

#include "psi/algorithm/ecdh/common.h"
#include "psi/cryptor/cryptor_selector.h"
#include "psi/trace_categories.h"
#include "psi/utils/sync.h"

#include "psi/proto/psi.pb.h"

namespace psi::ecdh {

EcdhPsiSender::EcdhPsiSender(const v2::PsiConfig &config,
                             std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiSender(config, std::move(lctx)) {
  trunc_intersection_indices_ = true;
}

void EcdhPsiSender::Init() {
  TRACE_EVENT("init", "EcdhPsiSender::Init");
  SPDLOG_INFO("[EcdhPsiSender::Init] start");

  AbstractPsiSender::Init();

  if (recovery_manager_) {
    recovery_manager_->MarkInitEnd(config_, keys_hash_);
  }

  SPDLOG_INFO("[EcdhPsiSender::Init] end");
}

void EcdhPsiSender::PreProcess() {
  TRACE_EVENT("pre-process", "EcdhPsiSender::PreProcess");
  SPDLOG_INFO("[EcdhPsiSender::PreProcess] start");

  if (digest_equal_) {
    return;
  }

  if (config_.protocol_config().ecdh_config().batch_size() != 0) {
    psi_options_.batch_size =
        config_.protocol_config().ecdh_config().batch_size();
  }

  psi_options_.ecc_cryptor =
      CreateEccCryptor(config_.protocol_config().ecdh_config().curve());
  psi_options_.link_ctx = lctx_;

  // NOTE(junfeng): Only difference between receiver and sender.
  psi_options_.target_rank = static_cast<size_t>(lctx_->Rank() == 0);
  if (config_.protocol_config().broadcast_result()) {
    psi_options_.target_rank = yacl::link::kAllRank;
  }

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
        dir_resource_->Path() / "self_ec_point_store", kDefaultBinNum);
    peer_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        dir_resource_->Path() / "peer_ec_point_store", kDefaultBinNum);
  }

  SPDLOG_INFO("[EcdhPsiSender::PreProcess] end");
}

void EcdhPsiSender::Online() {
  TRACE_EVENT("online", "EcdhPsiSender::Online");
  SPDLOG_INFO("[EcdhPsiSender::Online] start");

  if (digest_equal_) {
    return;
  }
  bool online_stage_finished =
      recovery_manager_ ? recovery_manager_->MarkOnlineStart(lctx_) : false;

  if (!online_stage_finished) {
    SyncWait(lctx_, [&] {
      RunEcdhPsi(psi_options_, batch_provider_, self_ec_point_store_,
                 peer_ec_point_store_);
    });
  }

  if (recovery_manager_) {
    recovery_manager_->MarkOnlineEnd();
  }

  SPDLOG_INFO("[EcdhPsiSender::Online] end");
}

void EcdhPsiSender::PostProcess() {
  TRACE_EVENT("post-process", "EcdhPsiSender::PostProcess");
  SPDLOG_INFO("[EcdhPsiSender::PostProcess] start");

  if (digest_equal_) {
    return;
  }

  SyncWait(lctx_, [&] {
    (void)FinalizeAndComputeIndices(self_ec_point_store_, peer_ec_point_store_,
                                    intersection_indices_writer_.get());
  });

  if (recovery_manager_) {
    recovery_manager_->MarkPostProcessEnd();
  }

  SPDLOG_INFO("[EcdhPsiSender::PostProcess] end");
}

}  // namespace psi::ecdh
