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

#include <filesystem>
#include <string>
#include <vector>

#include "yacl/link/link.h"

#include "psi/proto/psi.pb.h"

namespace psi::psi {

// Generate InnerJoinConfig from PsiConfig.
v2::InnerJoinConfig BuildInnerJoinConfig(
    const v2::PsiConfig& psi_config,
    const std::filesystem::path& root = std::filesystem::temp_directory_path());

// Generate sorted input at context.sorted_input_path
void InnerJoinGenerateSortedInput(const v2::InnerJoinConfig& config);

// Generate unique keys and their cnt at context.unique_input_keys_cnt_path.
void InnerJoinGenerateUniqueInputKeysCnt(const v2::InnerJoinConfig& config);

// After PSI with config.unique_input_keys_cnt_path as input and
// config.self_intersection_cnt_path as output.
// Run SyncIntersectionCnt to sync and save count at
// config.peer_intersection_cnt_path
void InnerJoinSyncIntersectionCnt(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    const v2::InnerJoinConfig& config);

// Generate inner join intersection at config.output_path_path
void InnerJoinGenerateIntersection(const v2::InnerJoinConfig& config);

// Generate inner join intersection at config.output_path_path
void InnerJoinGenerateDifference(const v2::InnerJoinConfig& config);

}  // namespace psi::psi
