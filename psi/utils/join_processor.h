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

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "index_store.h"
#include "yacl/link/link.h"

#include "psi/utils/index_store.h"
#include "psi/utils/table_utils.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

class JoinProcessor {
 public:
  inline static const std::string kNullRep = "NULL";

  static std::shared_ptr<JoinProcessor> Make(const v2::PsiConfig& psi_config,
                                             const std::filesystem::path& root);

  static std::shared_ptr<JoinProcessor> Make(const v2::UbPsiConfig& psi_config,
                                             const std::filesystem::path& root);

  std::shared_ptr<KeyInfo> GetUniqueKeysInfo();

  void GenerateResult(uint32_t peer_except_cnt);

  KeyInfo::StatInfo DealResultIndex(IndexReader& index);

 private:
  std::shared_ptr<Table> GetInputTable();
  std::shared_ptr<SortedTable> GetSortedInputTable();
  std::shared_ptr<UniqueKeyTable> GetUniqueKeyTable();

  JoinProcessor(const v2::PsiConfig& psi_config,
                const std::filesystem::path& root);

  JoinProcessor(const v2::UbPsiConfig& psi_config,
                const std::filesystem::path& root);

  void CheckUbPsiClientConfig(const v2::UbPsiConfig& ub_psi_config,
                              const std::string& prefix,
                              const std::filesystem::path& root);
  void CheckUbPsiServerConfig(const v2::UbPsiConfig& ub_psi_config);

  // Origin input file.
  std::string input_path_;

  // TODO(huocun): ub psi support this condition
  bool is_input_key_unique_ = false;
  // Keys for PSI.
  std::vector<std::string> keys_;

  std::string output_path_;
  std::string csv_null_rep_ = "NULL";
  bool align_output_ = true;

  // Join type.
  v2::PsiConfig::AdvancedJoinType type_ =
      v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED;

  // The role of party,
  v2::Role role_ = v2::Role::ROLE_UNSPECIFIED;
  v2::Role left_side_ = v2::Role::ROLE_UNSPECIFIED;

  // File to save sorted input depending on keys.
  std::string sorted_input_path_;
  std::string sorted_intersect_path_;
  std::string sorted_except_path_;
  std::string key_info_path_;

  std::shared_ptr<Table> input_table_;
  std::shared_ptr<SortedTable> sorted_table_;
  std::shared_ptr<UniqueKeyTable> unique_table_;
  std::shared_ptr<KeyInfo> input_table_keys_info_;
};

}  // namespace psi
