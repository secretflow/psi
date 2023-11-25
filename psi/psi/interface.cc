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

#include "psi/psi/interface.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>

#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "yacl/base/exception.h"
#include "yacl/link/link.h"

#include "psi/psi/bucket_psi.h"
#include "psi/psi/prelude.h"
#include "psi/psi/trace_categories.h"
#include "psi/psi/utils/csv_checker.h"
#include "psi/psi/utils/inner_join.h"
#include "psi/psi/utils/utils.h"

#include "psi/proto/psi.pb.h"

#include "boost/uuid/uuid.hpp"            // uuid class
#include "boost/uuid/uuid_generators.hpp" // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

namespace psi::psi {

namespace {

std::string GenerateIndexFileName(v2::Role role) {
  return fmt::format("psi_index_{}.csv", role);
}

std::string GenerateSortedIndexFileName(v2::Role role) {
  return fmt::format("sorted_psi_index_{}.csv", role);
}

std::string GenerateIndexFileName() {
  boost::uuids::random_generator uuid_generator;
  return fmt::format("psi_index_{}.csv",
                     boost::uuids::to_string(uuid_generator()));
}

std::string GenerateSortedIndexFileName() {
  boost::uuids::random_generator uuid_generator;
  return fmt::format("sorted_psi_index_{}.csv",
                     boost::uuids::to_string(uuid_generator()));
}

}  // namespace

constexpr size_t kIndexWriterBatchSize = 1 << 10;

AbstractPSIParty::AbstractPSIParty(const v2::PsiConfig &config, v2::Role role,
                                   std::shared_ptr<yacl::link::Context> lctx)
    : config_(config),
      role_(role),
      selected_keys_(config_.keys().begin(), config_.keys().end()),
      lctx_(std::move(lctx)) {}

void AbstractPSIParty::Init() {
  TRACE_EVENT("init", "AbstractPSIParty::Init");
  SPDLOG_INFO("[AbstractPSIParty::Init] start");

  if (!lctx_) {
    yacl::link::ContextDesc lctx_desc(config_.link_config());
    int rank = -1;
    for (int i = 0; i < config_.link_config().parties().size(); i++) {
      if (config_.link_config().parties(i).id() == config_.self_link_party()) {
        rank = i;
      }
    }
    YACL_ENFORCE_GE(rank, 0, "Couldn't find rank in YACL Link.");

    lctx_ = yacl::link::FactoryBrpc().CreateContext(lctx_desc, rank);
  }

  // Test connection.
  lctx_->ConnectToMesh();

  CheckSelfConfig();

  CheckPeerConfig();

  if (config_.advanced_join_type() ==
      v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    SPDLOG_INFO("[AbstractPSIParty::Init][Inner join pre-process] start");

    if (config_.recovery_config().enabled()) {
      inner_join_config_ =
          std::make_shared<v2::InnerJoinConfig>(BuildInnerJoinConfig(
              config_,
              std::filesystem::path(config_.recovery_config().folder())));
    } else {
      inner_join_config_ =
          std::make_shared<v2::InnerJoinConfig>(BuildInnerJoinConfig(config_));
    }

    config_.mutable_input_config()->set_path(
        inner_join_config_->unique_input_keys_cnt_path());
    config_.mutable_output_config()->set_path(
        inner_join_config_->self_intersection_cnt_path());

    auto inner_join_preprocess_f = std::async([&] {
      InnerJoinGenerateSortedInput(*inner_join_config_);
      InnerJoinGenerateUniqueInputKeysCnt(*inner_join_config_);
    });

    SyncWait(lctx_, &inner_join_preprocess_f);

    SPDLOG_INFO("[AbstractPSIParty::Init][Inner join pre-process] end");
  }

  auto check_csv_f = std::async([&] {
    if (config_.check_duplicates() || config_.check_hash_digest() ||
        config_.protocol_config().protocol() != v2::PROTOCOL_ECDH) {
      SPDLOG_INFO("[AbstractPSIParty::Init][Check csv pre-process] start");

      CheckCsvReport check_csv_report =
          CheckCsv(config_.input_config().path(), selected_keys_,
                   config_.check_duplicates(), config_.check_hash_digest());

      key_hash_digest_ = check_csv_report.key_hash_digest;

      report_.set_original_count(check_csv_report.num_rows);

      SPDLOG_INFO("[AbstractPSIParty::Init][Check csv pre-process] end");
    }
  });

  SyncWait(lctx_, &check_csv_f);

  // Check if the input are the same between receiver and sender.
  if (config_.check_hash_digest()) {
    std::vector<yacl::Buffer> digest_buf_list =
        yacl::link::AllGather(lctx_, key_hash_digest_, "PSI:SYNC_DIGEST");
    digest_equal_ = HashListEqualTest(digest_buf_list);
  }

  if (config_.recovery_config().enabled()) {
    recovery_manager_ =
        std::make_shared<RecoveryManager>(config_.recovery_config().folder());
  }

  std::filesystem::path intersection_indices_writer_path =
      recovery_manager_
          ? std::filesystem::path(config_.recovery_config().folder()) /
                GenerateIndexFileName(role_)
          : std::filesystem::path(config_.output_config().path())
                    .parent_path() /
                GenerateIndexFileName();

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
  SPDLOG_INFO("[AbstractPSIParty::Init] end");
}

v2::PsiReport AbstractPSIParty::Finalize() {
  TRACE_EVENT("finalize", "AbstractPSIParty::Finalize");
  SPDLOG_INFO("[AbstractPSIParty::Finalize] start");

  intersection_indices_writer_->Close();

  bool sort_output = config_.sort_output();
  if (!sort_output && inner_join_config_) {
    SPDLOG_WARN(
        "Sort_output turns off while inner_join is selected. Sort_output is "
        "enabled.");

    sort_output = false;
  }

  std::filesystem::path sorted_intersection_indices_path =
      intersection_indices_writer_->path().parent_path() /
      (recovery_manager_ ? GenerateSortedIndexFileName(role_)
                         : GenerateSortedIndexFileName());

  SPDLOG_INFO("[AbstractPSIParty::Finalize][Generate result] start");
  auto gen_result_f = std::async([&] {
    MultiKeySort(
        intersection_indices_writer_->path(), sorted_intersection_indices_path,
        std::vector<std::string>{kIdx}, true, need_intersection_deduplication_);

    if (role_ == v2::ROLE_RECEIVER ||
        config_.protocol_config().broadcast_result()) {
      report_.set_intersection_count(GenerateResult(
          config_.input_config().path(), config_.output_config().path(),
          selected_keys_, sorted_intersection_indices_path, sort_output,
          digest_equal_, !inner_join_config_ && config_.output_difference()));
    }
  });

  SyncWait(lctx_, &gen_result_f);
  SPDLOG_INFO("[AbstractPSIParty::Finalize][Generate result] end");

  if (inner_join_config_ && !config_.output_difference()) {
    SPDLOG_INFO("[AbstractPSIParty::Finalize][Inner join post-precess] start");
    auto sync_intersection_f = std::async([&] {
      return InnerJoinSyncIntersectionCnt(lctx_, *inner_join_config_);
    });

    SyncWait(lctx_, &sync_intersection_f);
    SPDLOG_INFO("[AbstractPSIParty::Finalize][Inner join post-precess] end");
  }

  lctx_->WaitLinkTaskFinish();

  if (inner_join_config_) {
    SPDLOG_INFO("[AbstractPSIParty::Finalize][Generate difference] start");
    if (config_.output_difference()) {
      InnerJoinGenerateDifference(*inner_join_config_);
    } else {
      InnerJoinGenerateIntersection(*inner_join_config_);
    }
    SPDLOG_INFO("[AbstractPSIParty::Finalize][Generate difference] end");
  }

  if (role_ == v2::ROLE_SENDER &&
      !config_.protocol_config().broadcast_result()) {
    report_.set_intersection_count(-1);
  }

  SPDLOG_INFO("[AbstractPSIParty::Finalize] end");

  return report_;
}

void AbstractPSIParty::CheckSelfConfig() {
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

  if (config_.advanced_join_type() ==
      v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN) {
    YACL_THROW("left join is unsupported.");
  }

  if (!config_.protocol_config().broadcast_result() &&
      config_.advanced_join_type() ==
          v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    SPDLOG_WARN(
        "broadcast_result turns off while inner_join is selected. "
        "broadcast_result is modified to true since intersection has to be "
        "sent to both parties.");

    YACL_ENFORCE(!config_.output_config().path().empty(),
                 "You have to provide path of output.");

    config_.mutable_protocol_config()->set_broadcast_result(true);
  }

  if (config_.check_duplicates() &&
      config_.advanced_join_type() ==
          v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    SPDLOG_WARN(
        "check_duplicates turns on while inner_join is selected. "
        "check_duplicates is ignored.");

    config_.set_check_duplicates(false);
  }

  if (!config_.check_hash_digest() && config_.recovery_config().enabled()) {
    SPDLOG_WARN(
        "check_hash_digest turns off while recovery is enabled. "
        "check_hash_digest is modified to true for robustness.");

    config_.set_check_hash_digest(true);
  }
}

void AbstractPSIParty::CheckPeerConfig() {
  v2::PsiConfig config = config_;

  // The fields below don't need to verify.
  config.mutable_input_config()->Clear();
  config.mutable_output_config()->Clear();
  config.mutable_link_config()->Clear();
  config.set_self_link_party("");
  config.mutable_keys()->Clear();
  config.mutable_debug_options()->Clear();
  config.set_check_duplicates(false);
  config.set_output_difference(false);
  config.set_sort_output(false);

  // Recovery must be enabled by all parties at the same time.
  config.mutable_recovery_config()->set_folder("");

  std::string serialized;
  YACL_ENFORCE(config.SerializeToString(&serialized));

  std::vector<yacl::Buffer> serialized_list =
      yacl::link::AllGather(lctx_, serialized, "PSI:VERIFY_CONFIG");

  YACL_ENFORCE_EQ(serialized_list.size(), 2UL);

  if (!(serialized_list[0] == serialized_list[1])) {
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
}

AbstractPSIReceiver::AbstractPSIReceiver(
    const v2::PsiConfig &config, std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPSIParty(config, v2::Role::ROLE_RECEIVER, std::move(lctx)) {}

AbstractPSISender::AbstractPSISender(const v2::PsiConfig &config,
                                     std::shared_ptr<yacl::link::Context> lctx)
    : AbstractPSIParty(config, v2::Role::ROLE_SENDER, std::move(lctx)) {}

}  // namespace psi::psi
