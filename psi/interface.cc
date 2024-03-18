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

#include <spdlog/spdlog.h>

#include <cassert>
#include <cstddef>
#include <filesystem>

#include "absl/time/time.h"
#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "google/protobuf/util/message_differencer.h"
#include "yacl/base/exception.h"
#include "yacl/link/link.h"

#include "psi/legacy/bucket_psi.h"
#include "psi/prelude.h"
#include "psi/trace_categories.h"
#include "psi/utils/advanced_join.h"
#include "psi/utils/csv_checker.h"
#include "psi/utils/key.h"
#include "psi/utils/sync.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

namespace {

std::string GenerateIndexFileName(v2::Role role = v2::ROLE_UNSPECIFIED) {
  if (role) {
    return fmt::format("psi_index_{}.csv", role);
  } else {
    boost::uuids::random_generator uuid_generator;
    return fmt::format("psi_index_{}.csv",
                       boost::uuids::to_string(uuid_generator()));
  }
}

std::string GenerateSortedIndexFileName(v2::Role role = v2::ROLE_UNSPECIFIED) {
  if (role) {
    return fmt::format("sorted_psi_index_{}.csv", role);
  } else {
    boost::uuids::random_generator uuid_generator;
    return fmt::format("sorted_psi_index_{}.csv",
                       boost::uuids::to_string(uuid_generator()));
  }
}

}  // namespace

constexpr size_t kIndexWriterBatchSize = 1 << 10;

AbstractPsiParty::AbstractPsiParty(const v2::PsiConfig &config, v2::Role role,
                                   std::shared_ptr<yacl::link::Context> lctx)
    : config_(config),
      role_(role),
      selected_keys_(config_.keys().begin(), config_.keys().end()),
      lctx_(std::move(lctx)) {}

void AbstractPsiParty::Init() {
  TRACE_EVENT("init", "AbstractPsiParty::Init");
  SPDLOG_INFO("[AbstractPsiParty::Init] start");

  CheckSelfConfig();

  assert(lctx_);

  // Test connection.
  lctx_->ConnectToMesh();

  CheckPeerConfig();

  if (config_.advanced_join_type() !=
      v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED) {
    SPDLOG_INFO("[AbstractPsiParty::Init][Advanced join pre-process] start");

    if (config_.recovery_config().enabled()) {
      advanced_join_config_ =
          std::make_shared<AdvancedJoinConfig>(BuildAdvancedJoinConfig(
              config_,
              std::filesystem::path(config_.recovery_config().folder())));
    } else {
      advanced_join_config_ = std::make_shared<AdvancedJoinConfig>(
          BuildAdvancedJoinConfig(config_));
    }

    config_.mutable_input_config()->set_path(
        advanced_join_config_->unique_input_keys_cnt_path);
    config_.mutable_output_config()->set_path(
        advanced_join_config_->self_intersection_cnt_path);

    auto advanced_join_preprocess_f = std::async(
        [&] { AdvancedJoinPreprocess(advanced_join_config_.get()); });

    SyncWait(lctx_, &advanced_join_preprocess_f);

    SPDLOG_INFO("[AbstractPsiParty::Init][Advanced join pre-process] end");
  }

  if (config_.recovery_config().enabled()) {
    recovery_manager_ =
        std::make_shared<RecoveryManager>(config_.recovery_config().folder());
  }

  bool check_duplicates = !config_.skip_duplicates_check();
  if (check_duplicates && recovery_manager_) {
    if (recovery_manager_->checkpoint().stage() >=
        v2::RecoveryCheckpoint::STAGE_INIT_END) {
      SPDLOG_WARN(
          "A previous recovery checkpoint is found, duplicates check is "
          "skipped.");
      check_duplicates = false;
    }
  }

  CheckCsvReport check_csv_report;
  auto check_csv_f = std::async([&] {
    if (check_duplicates || config_.check_hash_digest() ||
        config_.protocol_config().protocol() != v2::PROTOCOL_ECDH) {
      SPDLOG_INFO("[AbstractPsiParty::Init][Check csv pre-process] start");

      check_csv_report =
          CheckCsv(config_.input_config().path(), selected_keys_,
                   check_duplicates, config_.check_hash_digest());

      key_hash_digest_ = check_csv_report.key_hash_digest;
      report_.set_original_count(check_csv_report.num_rows);

      SPDLOG_INFO("[AbstractPsiParty::Init][Check csv pre-process] end");
    }
  });

  SyncWait(lctx_, &check_csv_f);

  YACL_ENFORCE(
      !check_csv_report.contains_duplicates,
      "Input file {} contains duplicates, please check duplicates at {}",
      config_.input_config().path(),
      check_csv_report.duplicates_keys_file_path);

  // Check if the input are the same between receiver and sender.
  if (config_.check_hash_digest()) {
    std::vector<yacl::Buffer> digest_buf_list =
        yacl::link::AllGather(lctx_, key_hash_digest_, "PSI:SYNC_DIGEST");
    digest_equal_ = HashListEqualTest(digest_buf_list);
  }

  std::filesystem::path intersection_indices_writer_path =
      recovery_manager_
          ? std::filesystem::path(config_.recovery_config().folder()) /
                GenerateIndexFileName(role_)
          : std::filesystem::temp_directory_path() / GenerateIndexFileName();

  intersection_indices_writer_ = std::make_shared<IndexWriter>(
      intersection_indices_writer_path, kIndexWriterBatchSize,
      trunc_intersection_indices_);

  if (digest_equal_) {
    SPDLOG_WARN("The keys between two parties share the same set.");
    for (int64_t i = 0; i < report_.original_count(); i++) {
      if (intersection_indices_writer_->WriteCache(i) ==
          kIndexWriterBatchSize) {
        intersection_indices_writer_->Commit();
      }
    }
  }
  SPDLOG_INFO("[AbstractPsiParty::Init] end");
}

PsiResultReport AbstractPsiParty::Finalize() {
  TRACE_EVENT("finalize", "AbstractPsiParty::Finalize");
  SPDLOG_INFO("[AbstractPsiParty::Finalize] start");

  intersection_indices_writer_->Close();

  std::filesystem::path sorted_intersection_indices_path =
      intersection_indices_writer_->path().parent_path() /
      (recovery_manager_ ? GenerateSortedIndexFileName(role_)
                         : GenerateSortedIndexFileName());

  bool sort_output = !config_.disable_alignment();
  if (advanced_join_config_) {
    sort_output = false;
  }

  SPDLOG_INFO("[AbstractPsiParty::Finalize][Generate result] start");
  auto gen_result_f = std::async([&] {
    MultiKeySort(intersection_indices_writer_->path(),
                 sorted_intersection_indices_path,
                 std::vector<std::string>{kIdx}, true, false);

    if (role_ == v2::ROLE_RECEIVER ||
        config_.protocol_config().broadcast_result()) {
      report_.set_intersection_count(GenerateResult(
          config_.input_config().path(), config_.output_config().path(),
          selected_keys_, sorted_intersection_indices_path, sort_output,
          digest_equal_, false));
    }
  });

  SyncWait(lctx_, &gen_result_f);
  SPDLOG_INFO("[AbstractPsiParty::Finalize][Generate result] end");

  if (advanced_join_config_) {
    SPDLOG_INFO("[AbstractPsiParty::Finalize][Advanced join sync] start");
    auto sync_intersection_f = std::async(
        [&] { return AdvancedJoinSync(lctx_, advanced_join_config_.get()); });

    SyncWait(lctx_, &sync_intersection_f);
    SPDLOG_INFO("[AbstractPsiParty::Finalize][Advanced join sync] end");

    SPDLOG_INFO(
        "[AbstractPsiParty::Finalize][Advanced join generate result] start");
    AdvancedJoinGenerateResult(*advanced_join_config_);
    SPDLOG_INFO(
        "[AbstractPsiParty::Finalize][Advanced join generate result] end");

    report_.set_intersection_count(
        advanced_join_config_->self_intersection_cnt);
  } else {
    if (role_ == v2::ROLE_SENDER &&
        !config_.protocol_config().broadcast_result()) {
      report_.set_intersection_count(-1);
    }
  }

  // remove result indices records.
  if (!recovery_manager_) {
    std::error_code ec;
    std::filesystem::remove(sorted_intersection_indices_path, ec);
    std::filesystem::remove(intersection_indices_writer_->path(), ec);
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
                  "Duplicated key is provided.");

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
