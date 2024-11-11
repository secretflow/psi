// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/utils/join_processor.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include "spdlog/spdlog.h"
#include "table_utils.h"
#include "yacl/base/exception.h"

#include "psi/utils/io.h"
#include "psi/utils/random_str.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

std::shared_ptr<JoinProcessor> JoinProcessor::Make(
    const v2::PsiConfig& psi_config, const std::filesystem::path& root) {
  YACL_ENFORCE(std::filesystem::exists(psi_config.input_config().path()),
               "input file {} not exists.", psi_config.input_config().path());
  if (!std::filesystem::exists(root)) {
    SPDLOG_INFO("create file path: {}", root.string());
    std::filesystem::create_directories(root);
  }

  return std::shared_ptr<JoinProcessor>(new JoinProcessor(psi_config, root));
}

std::shared_ptr<JoinProcessor> JoinProcessor::Make(
    const v2::UbPsiConfig& psi_config, const std::filesystem::path& root) {
  return std::shared_ptr<JoinProcessor>(new JoinProcessor(psi_config, root));
}

void JoinProcessor::CheckUbPsiServerConfig(
    const v2::UbPsiConfig& ub_psi_config) {
  if (!std::filesystem::exists(ub_psi_config.cache_path())) {
    YACL_ENFORCE(std::filesystem::exists(ub_psi_config.input_config().path()),
                 "input file {} not exists.",
                 ub_psi_config.input_config().path());
  }

  std::set<v2::UbPsiConfig::Mode> need_input_mode = {
      v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE,
      v2::UbPsiConfig::MODE_OFFLINE,
      v2::UbPsiConfig::MODE_FULL,
  };
  if (need_input_mode.find(ub_psi_config.mode()) != need_input_mode.end()) {
    YACL_ENFORCE(
        ub_psi_config.input_config().type() == v2::IoType::IO_TYPE_FILE_CSV,
        "unsupport input format {}",
        v2::IoType_Name(ub_psi_config.input_config().type()));
    input_path_ = ub_psi_config.input_config().path();
    SPDLOG_INFO("Clean cache path: {}", ub_psi_config.cache_path());
    std::filesystem::remove_all(ub_psi_config.cache_path());
  }

  std::filesystem::path cache_path = ub_psi_config.cache_path();
  sorted_input_path_ = cache_path / ("join_sorted_input.csv");
  key_info_path_ = cache_path / ("join_sorted_input_key_info.csv");
}

void JoinProcessor::CheckUbPsiClientConfig(const v2::UbPsiConfig& ub_psi_config,
                                           const std::string& prefix,
                                           const std::filesystem::path& root) {
  std::set<v2::UbPsiConfig::Mode> need_input_mode = {
      v2::UbPsiConfig::MODE_ONLINE,
      v2::UbPsiConfig::MODE_FULL,
  };
  if (need_input_mode.find(ub_psi_config.mode()) != need_input_mode.end()) {
    YACL_ENFORCE(
        ub_psi_config.input_config().type() == v2::IoType::IO_TYPE_FILE_CSV,
        "unsupport input format {}",
        v2::IoType_Name(ub_psi_config.input_config().type()));
    input_path_ = ub_psi_config.input_config().path();
  }

  sorted_input_path_ = root / (prefix + "join_sorted_input.csv");
  key_info_path_ = root / (prefix + "join_sorted_input_key_info.csv");
}

JoinProcessor::JoinProcessor(const v2::UbPsiConfig& ub_psi_config,
                             const std::filesystem::path& root) {
  YACL_ENFORCE(!ub_psi_config.cache_path().empty(),
               "cache path should not be empty.");

  role_ = ub_psi_config.role();
  YACL_ENFORCE((role_ == v2::ROLE_SERVER) || (role_ == v2::ROLE_CLIENT),
               "role: {} is not a valid role.", v2::Role_Name(role_));

  std::string prefix =
      fmt::format("{}_{}_", role_ == v2::ROLE_SERVER ? "server" : "client",
                  GetRandomString());

  if (role_ == v2::ROLE_SERVER) {
    CheckUbPsiServerConfig(ub_psi_config);
  } else {
    CheckUbPsiClientConfig(ub_psi_config, prefix, root);
  }

  std::set<v2::UbPsiConfig::Mode> gen_output_mode = {
      v2::UbPsiConfig::MODE_ONLINE,
      v2::UbPsiConfig::MODE_FULL,
  };

  if (gen_output_mode.find(ub_psi_config.mode()) != gen_output_mode.end()) {
    YACL_ENFORCE(
        ub_psi_config.output_config().type() == v2::IoType::IO_TYPE_FILE_CSV,
        "unsupport output format {}",
        v2::IoType_Name(ub_psi_config.input_config().type()));
    output_path_ = ub_psi_config.output_config().path();
  }

  if (!std::filesystem::exists(ub_psi_config.cache_path())) {
    SPDLOG_INFO("create file path: {}", ub_psi_config.cache_path());
    std::filesystem::create_directories(ub_psi_config.cache_path());
  }

  keys_ = std::vector<std::string>(ub_psi_config.keys().begin(),
                                   ub_psi_config.keys().end());

  type_ = ub_psi_config.advanced_join_type();
  if (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED) {
    type_ = v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN;
  }

  left_side_ = ub_psi_config.left_side();
  if (type_ != v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    YACL_ENFORCE(left_side_ != v2::Role::ROLE_UNSPECIFIED,
                 "a valid left side must be provided.");
  }

  if (type_ != v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE) {
    sorted_intersect_path_ = root / (prefix + "join_sorted_input.inter.csv");
  }

  if (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE ||
      type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN ||
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN &&
       left_side_ == role_) ||
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN &&
       left_side_ != role_)) {
    sorted_except_path_ = root / (prefix + "join_sorted_input.except.csv");
  }

  if (!ub_psi_config.output_attr().csv_null_rep().empty()) {
    csv_null_rep_ = ub_psi_config.output_attr().csv_null_rep();
  }
  align_output_ = ub_psi_config.disable_alignment();
}

JoinProcessor::JoinProcessor(const v2::PsiConfig& psi_config,
                             const std::filesystem::path& root) {
  // TODO: support more format, CSV is only option for now
  YACL_ENFORCE(psi_config.input_config().type() == v2::IoType::IO_TYPE_FILE_CSV,
               "unsupport input format {}",
               v2::IoType_Name(psi_config.input_config().type()));
  input_path_ = psi_config.input_config().path();
  is_input_key_unique_ = psi_config.input_attr().keys_unique();
  YACL_ENFORCE(
      psi_config.output_config().type() == v2::IoType::IO_TYPE_FILE_CSV,
      "unsupport output format {}",
      v2::IoType_Name(psi_config.input_config().type()));
  output_path_ = psi_config.output_config().path();

  type_ = psi_config.advanced_join_type();
  if (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED) {
    type_ = v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN;
  }

  role_ = psi_config.protocol_config().role();
  YACL_ENFORCE((role_ == v2::ROLE_SENDER) || (role_ == v2::ROLE_RECEIVER),
               "role: {} is not a valid role.", v2::Role_Name(role_));

  left_side_ = psi_config.left_side();
  if (type_ != v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN) {
    YACL_ENFORCE(left_side_ != v2::Role::ROLE_UNSPECIFIED,
                 "a valid left side must be provided.");
  }

  keys_ = std::vector<std::string>(psi_config.keys().begin(),
                                   psi_config.keys().end());

  std::string prefix =
      fmt::format("{}_{}_", role_ == v2::ROLE_RECEIVER ? "receiver" : "sender",
                  GetRandomString(16));
  sorted_input_path_ = root / (prefix + "join_sorted_input.csv");
  key_info_path_ = root / (prefix + "join_sorted_input_key_info.csv");
  if (type_ != v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE) {
    sorted_intersect_path_ = root / (prefix + "join_sorted_input.inter.csv");
  }

  if (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE ||
      type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN ||
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN &&
       left_side_ == role_) ||
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN &&
       left_side_ != role_)) {
    sorted_except_path_ = root / (prefix + "join_sorted_input.except.csv");
  }

  if (!psi_config.output_attr().csv_null_rep().empty()) {
    csv_null_rep_ = psi_config.output_attr().csv_null_rep();
  }

  align_output_ = !psi_config.disable_alignment();
}

std::shared_ptr<UniqueKeyTable> JoinProcessor::GetUniqueKeyTable() {
  if (unique_table_ == nullptr) {
    unique_table_ = UniqueKeyTable::Make(input_path_, "csv", keys_);
  }
  return unique_table_;
}

std::shared_ptr<SortedTable> JoinProcessor::GetSortedInputTable() {
  if (sorted_table_ == nullptr) {
    if (std::filesystem::exists(sorted_input_path_)) {
      sorted_table_ = SortedTable::Make(sorted_input_path_, keys_);
    } else {
      sorted_table_ =
          SortedTable::Make(GetInputTable(), sorted_input_path_, keys_);
    }
  }
  return sorted_table_;
}

std::shared_ptr<Table> JoinProcessor::GetInputTable() {
  if (input_table_ == nullptr) {
    input_table_ = Table::MakeFromCsv(input_path_);
  }
  return input_table_;
}

std::shared_ptr<KeyInfo> JoinProcessor::GetUniqueKeysInfo() {
  if (input_table_keys_info_ == nullptr) {
    if (!is_input_key_unique_) {
      input_table_keys_info_ =
          KeyInfo::Make(GetSortedInputTable(), key_info_path_);
    } else {
      input_table_keys_info_ = KeyInfo::Make(GetUniqueKeyTable());
    }
  }
  return input_table_keys_info_;
}

KeyInfo::StatInfo JoinProcessor::DealResultIndex(IndexReader& index) {
  ResultDumper dumper(sorted_intersect_path_, sorted_except_path_);
  auto stat = GetUniqueKeysInfo()->ApplyPeerDupCnt(index, dumper);
  if (is_input_key_unique_ && align_output_) {
    if (!sorted_intersect_path_.empty()) {
      Table::MakeFromCsv(sorted_intersect_path_)
          ->SortInplace(GetInputTable()->Columns());
    }
  }
  return stat;
}

void JoinProcessor::GenerateResult(uint32_t peer_except_cnt) {
  SPDLOG_INFO("start generate result file: {}, peer_except_cnt: {}",
              output_path_, peer_except_cnt);
  auto ofs = io::GetStdOutFileStream(output_path_);

  auto columns = GetUniqueKeysInfo()->SourceFileColumns();
  *ofs << MakeQuotedCsvLine(columns) << '\n';

  auto write_output = [&](std::string input_path) {
    if (input_path.empty()) {
      return;
    }

    std::string line;
    std::ifstream ifs(input_path);
    std::getline(ifs, line);

    while (std::getline(ifs, line)) {
      *ofs << line << '\n';
    }
  };

  auto write_na = [&](uint32_t na_line_cnt) {
    if (na_line_cnt == 0) {
      return;
    }
    auto size = columns.size();
    std::ostringstream oss;
    oss << csv_null_rep_;
    while (--size) {
      oss << ',' << csv_null_rep_;
    }
    oss << '\n';
    auto line = oss.str();
    while (na_line_cnt--) {
      *ofs << line;
    }
  };

  write_output(sorted_intersect_path_);

  if (role_ != left_side_ &&
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN ||
       type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE ||
       type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN)) {
    write_na(peer_except_cnt);
  }

  write_output(sorted_except_path_);

  if (role_ == left_side_ &&
      (type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN ||
       type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE ||
       type_ == v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN)) {
    write_na(peer_except_cnt);
  }

  SPDLOG_INFO("end generate result file: {}", output_path_);
}

}  // namespace psi
