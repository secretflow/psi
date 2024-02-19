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

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "yacl/link/link.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

struct AdvancedJoinConfig {
  // Origin input file.
  std::string input_path;

  // Join type.
  v2::PsiConfig::AdvancedJoinType type =
      v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED;

  // The role of party,
  v2::Role role = v2::Role::ROLE_UNSPECIFIED;

  //
  v2::Role left_side = v2::Role::ROLE_UNSPECIFIED;

  // Keys for PSI.
  std::vector<std::string> keys;

  // File to save sorted input depending on keys.
  std::string sorted_input_path;

  // File to unique keys and cnt
  std::string unique_input_keys_cnt_path;

  // File to save PSI output with unique_input_keys_cnt_path as input.
  std::string self_intersection_cnt_path;

  // File to save received peer intersection count.
  std::string peer_intersection_cnt_path;

  // File to save difference as output.
  std::string difference_output_path;

  // The file to save output.
  std::string output_path;

  int64_t self_total_cnt = 0;

  int64_t self_intersection_cnt = 0;

  int64_t self_difference_cnt = 0;

  int64_t peer_difference_cnt = 0;
};

AdvancedJoinConfig BuildAdvancedJoinConfig(v2::PsiConfig::AdvancedJoinType type,
                                           v2::Role role, v2::Role left_side,
                                           const std::vector<std::string>& keys,
                                           const std::string& input_path,
                                           const std::string& output_path,
                                           const std::filesystem::path& root);

// Generate AdvancedJoinConfig from PsiConfig.
AdvancedJoinConfig BuildAdvancedJoinConfig(
    const v2::PsiConfig& psi_config,
    const std::filesystem::path& root = std::filesystem::temp_directory_path());

// Sort origin input by keys at sorted_input_path, generate unique keys and
// their cnt at unique_input_keys_cnt_path.
void AdvancedJoinPreprocess(AdvancedJoinConfig* config);

// After PSI with config.unique_input_keys_cnt_path as input and
// config.self_intersection_cnt_path as output.
// Run this method to sync and save intersection count at
// config.peer_intersection_cnt_path
void AdvancedJoinSync(const std::shared_ptr<yacl::link::Context>& link_ctx,
                      AdvancedJoinConfig* config);

// Generate result at config.output_path
void AdvancedJoinGenerateResult(const AdvancedJoinConfig& config);

}  // namespace psi
