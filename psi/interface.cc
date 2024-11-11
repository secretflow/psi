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

#include "psi/interface.h"

#include <cassert>
#include <cstddef>
#include <filesystem>

#include "google/protobuf/util/message_differencer.h"
#include "spdlog/spdlog.h"
#include "utils/index_store.h"
#include "utils/join_processor.h"
#include "utils/recovery.h"
#include "utils/resource_manager.h"
#include "yacl/base/exception.h"
#include "yacl/link/link.h"

#include "psi/legacy/bucket_psi.h"
#include "psi/prelude.h"
#include "psi/trace_categories.h"
#include "psi/utils/bucket.h"
#include "psi/utils/key.h"
#include "psi/utils/random_str.h"
#include "psi/utils/sync.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

constexpr size_t kIndexWriterBatchSize = 1 << 10;

AbstractPsiParty::AbstractPsiParty(const v2::PsiConfig &config, v2::Role role,
                                   std::shared_ptr<yacl::link::Context> lctx)
    : config_(config),
      role_(role),
      selected_keys_(config_.keys().begin(), config_.keys().end()),
      lctx_(std::move(lctx)) {}

std::filesystem::path AbstractPsiParty::GetTaskDir() {
  if (config_.recovery_config().enabled()) {
    if (recovery_manager_ == nullptr) {
      recovery_manager_ =
          std::make_shared<RecoveryManager>(config_.recovery_config().folder());
    }
    return config_.recovery_config().folder();
  } else {
    if (dir_resource_ == nullptr) {
      dir_resource_ = ResourceManager::GetInstance().AddDirResouce(
          std::filesystem::temp_directory_path() / GetRandomString());
    }
    return dir_resource_->Path();
  }
}

void AbstractPsiParty::Init() {
  TRACE_EVENT("init", "AbstractPsiParty::Init");
  SPDLOG_INFO("[AbstractPsiParty::Init] start");

  CheckSelfConfig();

  assert(lctx_);

  // Test connection.
  lctx_->ConnectToMesh();

  CheckPeerConfig();

  join_processor_ = JoinProcessor::Make(config_, GetTaskDir());

  auto preprocess_f = std::async([&] {
    SPDLOG_INFO("[AbstractPsiParty::Init][Check csv pre-process] start");

    // TODO(huocun): construct batch provider according to input_attr field
    keys_info_ = join_processor_->GetUniqueKeysInfo();
    keys_hash_ = keys_info_->KeysHash();
    report_.set_original_count(keys_info_->OriginCnt());
    report_.set_original_key_count(keys_info_->KeyCnt());

    batch_provider_ = keys_info_->GetKeysProviderWithDupCnt();
    SPDLOG_INFO("[AbstractPsiParty::Init][Check csv pre-process] end");
  });
  SyncWait(lctx_, &preprocess_f);

  if (!config_.skip_duplicates_check()) {
    YACL_ENFORCE(keys_info_->DupKeyCnt() == 0,
                 "input file {} contains duplicates keys",
                 config_.input_config().path());
  }

  // Check if the input are the same between receiver and sender.
  if (config_.check_hash_digest()) {
    std::vector<yacl::Buffer> digest_buf_list = yacl::link::AllGather(
        lctx_, {keys_hash_.data(), keys_hash_.size()}, "PSI:SYNC_DIGEST");
    digest_equal_ = HashListEqualTest(digest_buf_list);
  }

  std::filesystem::path intersection_indices_writer_path =
      GetTaskDir() /
      fmt::format("intersection_indices_{}.csv", v2::Role_Name(role_));

  intersection_indices_writer_ = std::make_shared<IndexWriter>(
      intersection_indices_writer_path, kIndexWriterBatchSize,
      trunc_intersection_indices_);

  if (digest_equal_) {
    SPDLOG_WARN("The keys between two parties share the same set.");
    // FIXME(huocun): This is broken now.
    for (int64_t i = 0; i < report_.original_key_count(); i++) {
      if (intersection_indices_writer_->WriteCache(i) ==
          kIndexWriterBatchSize) {
        intersection_indices_writer_->Commit();
      }
    }
  }

  if (config_.protocol_config().protocol() == v2::Protocol::PROTOCOL_KKRT &&
      config_.protocol_config().kkrt_config().bucket_size() == 0) {
    config_.mutable_protocol_config()->mutable_kkrt_config()->set_bucket_size(
        kDefaultBucketSize);
  }
  if (config_.protocol_config().protocol() == v2::Protocol::PROTOCOL_RR22 &&
      config_.protocol_config().rr22_config().bucket_size() == 0) {
    config_.mutable_protocol_config()->mutable_rr22_config()->set_bucket_size(
        kDefaultBucketSize);
  }

  if (recovery_manager_) {
    recovery_manager_->MarkInitEnd(config_, keys_hash_);
  }
  SPDLOG_INFO("[AbstractPsiParty::Init] end");
}

PsiResultReport AbstractPsiParty::Finalize() {
  TRACE_EVENT("finalize", "AbstractPsiParty::Finalize");
  SPDLOG_INFO("[AbstractPsiParty::Finalize] start");

  intersection_indices_writer_->Close();

  std::filesystem::path sorted_intersection_indices_path =
      GetTaskDir() /
      fmt::format("sorted_intersection_indices_{}.csv", v2::Role_Name(role_));

  SPDLOG_INFO("[AbstractPsiParty::Finalize][Generate result] start");
  auto gen_result_f = std::async([&] {
    MultiKeySort(intersection_indices_writer_->path(),
                 sorted_intersection_indices_path,
                 std::vector<std::string>{kIdx}, true, false);

    if (role_ == v2::ROLE_RECEIVER ||
        config_.protocol_config().broadcast_result()) {
      FileIndexReader index_reader(sorted_intersection_indices_path);
      auto stat = join_processor_->DealResultIndex(index_reader);
      SPDLOG_INFO("Join stat: {}", stat.ToString());

      if (config_.protocol_config().broadcast_result()) {
        std::vector<size_t> items_size =
            AllGatherItemsSize(lctx_, stat.original_count);
        join_processor_->GenerateResult(items_size[lctx_->NextRank()] -
                                        stat.peer_intersection_count);
        SPDLOG_INFO("Peer table line: {}", items_size[lctx_->NextRank()]);
      } else {
        join_processor_->GenerateResult(0);
      }

      report_.set_intersection_count(stat.self_intersection_count);
      report_.set_intersection_key_count(stat.inter_unique_cnt);
    }
  });

  SyncWait(lctx_, &gen_result_f);
  SPDLOG_INFO("[AbstractPsiParty::Finalize][Generate result] end");

  if (role_ == v2::ROLE_SENDER &&
      !config_.protocol_config().broadcast_result()) {
    report_.set_intersection_count(-1);
  }

  SPDLOG_INFO("[AbstractPsiParty::Finalize] end");

  return report_;
}

void AbstractPsiParty::CheckSelfConfig() {
  if (config_.protocol_config().protocol() == v2::PROTOCOL_ECDH) {
    if (config_.protocol_config().ecdh_config().curve() == CURVE_INVALID_TYPE) {
      YACL_THROW("Curve type is not specified.");
    }
  }

  if (config_.protocol_config().role() != role_) {
    YACL_THROW("Role doesn't match.");
  }

  if (config_.input_config().type() != v2::IO_TYPE_FILE_CSV) {
    YACL_THROW("Input type only supports IO_TYPE_FILE_CSV at this moment.");
  }

  if (config_.output_config().type() != v2::IO_TYPE_FILE_CSV) {
    YACL_THROW("Output type only supports IO_TYPE_FILE_CSV at this moment.");
  }

  if (config_.keys().empty()) {
    YACL_THROW("keys are not specified.");
  }

  std::set<std::string> keys_set(config_.keys().begin(), config_.keys().end());
  YACL_ENFORCE_EQ(static_cast<int>(keys_set.size()), config_.keys().size(),
                  "Duplicated key is not allowed.");

  if (!config_.protocol_config().broadcast_result() &&
      config_.advanced_join_type() !=
          v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED) {
    SPDLOG_WARN(
        "broadcast_result turns off while advanced join is enabled. "
        "broadcast_result is modified to true since intersection has to be "
        "sent to both parties.");

    YACL_ENFORCE(!config_.output_config().path().empty(),
                 "You have to provide path of output.");

    config_.mutable_protocol_config()->set_broadcast_result(true);
  }

  if (!config_.skip_duplicates_check() &&
      config_.advanced_join_type() !=
          v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED) {
    SPDLOG_WARN(
        "The check of duplicated items will be skiped while advanced join "
        "is enabled. ");

    config_.set_skip_duplicates_check(true);
  }

  if (!config_.check_hash_digest() && config_.recovery_config().enabled()) {
    SPDLOG_WARN(
        "check_hash_digest turns off while recovery is enabled. "
        "check_hash_digest is modified to true for robustness.");

    config_.set_check_hash_digest(true);
  }
}

void AbstractPsiParty::CheckPeerConfig() {
  v2::PsiConfig config = config_;

  // The fields below don't need to verify.
  config.mutable_input_config()->Clear();
  config.mutable_output_config()->Clear();
  config.mutable_keys()->Clear();
  config.mutable_debug_options()->Clear();
  config.set_skip_duplicates_check(false);
  config.set_disable_alignment(false);
  config.mutable_input_attr()->set_keys_unique(false);

  // Recovery must be enabled by all parties at the same time.
  config.mutable_recovery_config()->set_folder("");

  std::string serialized;
  YACL_ENFORCE(config.SerializeToString(&serialized));

  std::vector<yacl::Buffer> serialized_list =
      yacl::link::AllGather(lctx_, serialized, "PSI:VERIFY_CONFIG");

  YACL_ENFORCE_EQ(serialized_list.size(), 2UL);

  const std::string rank0_serialized(serialized_list[0].data<char>(),
                                     serialized_list[0].size());

  const std::string rank1_serialized(serialized_list[1].data<char>(),
                                     serialized_list[1].size());

  v2::PsiConfig rank0_config;
  YACL_ENFORCE(rank0_config.ParseFromString(rank0_serialized));

  v2::PsiConfig rank1_config;
  YACL_ENFORCE(rank1_config.ParseFromString(rank1_serialized));

  if (rank0_config.protocol_config().role() ==
      rank1_config.protocol_config().role()) {
    YACL_THROW("The role of parties must be different.");
  }

  rank0_config.mutable_protocol_config()->set_role(v2::ROLE_UNSPECIFIED);
  rank1_config.mutable_protocol_config()->set_role(v2::ROLE_UNSPECIFIED);

  YACL_ENFORCE(::google::protobuf::util::MessageDifferencer::Equals(
                   rank0_config, rank1_config),
               "PSI configs are not consistent between parties. Rank 0: {} "
               "while Rank 1: {}",
               rank0_config.ShortDebugString(),
               rank1_config.ShortDebugString());
}

PsiResultReport AbstractUbPsiParty::Run() {
  Init();

  switch (config_.mode()) {
    case v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE: {
      OfflineGenCache();
      break;
    }
    case v2::UbPsiConfig::MODE_OFFLINE_TRANSFER_CACHE: {
      OfflineTransferCache();
      break;
    }
    case v2::UbPsiConfig::MODE_OFFLINE: {
      Offline();
      break;
    }
    case v2::UbPsiConfig::MODE_ONLINE: {
      Online();
      break;
    }
    case v2::UbPsiConfig::MODE_FULL: {
      Offline();
      Online();
      break;
    }
    default: {
      YACL_THROW("unsupported mode.");
    }
  }

  return report_;
}

AbstractPsiReceiver::AbstractPsiReceiver(
    const v2::PsiConfig &config, std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiParty(config, v2::Role::ROLE_RECEIVER, std::move(lctx)) {}

AbstractPsiSender::AbstractPsiSender(const v2::PsiConfig &config,
                                     std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPsiParty(config, v2::Role::ROLE_SENDER, std::move(lctx)) {}

AbstractUbPsiParty::AbstractUbPsiParty(
    const v2::UbPsiConfig &config, v2::Role role,
    std::shared_ptr<yacl::link::Context> lctx)
    : config_(config), role_(role), lctx_(std::move(lctx)) {}

AbstractUbPsiServer::AbstractUbPsiServer(
    const v2::UbPsiConfig &config, std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiParty(config, v2::Role::ROLE_SERVER, std::move(lctx)) {}

AbstractUbPsiClient::AbstractUbPsiClient(
    const v2::UbPsiConfig &config, std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiParty(config, v2::Role::ROLE_CLIENT, std::move(lctx)) {}

}  // namespace psi
