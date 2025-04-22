// Copyright 2025 Ant Group Co., Ltd.
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

#include <string>
#include <vector>

#include "psi/config/protocol.h"

namespace psi::api {

// Advanced Join allow duplicate keys.
// - If selected, duplicates_check is skipped.
// - If selected, both parties are allowed to contain duplicate keys.
// - If use left join, full join or difference, the size of difference set of
// left party is revealed to right party.
// - If use right join, full join or difference, the size of difference set of
// right party is revealed to left party.
enum class ResultJoinType : uint8_t {
  JOIN_TYPE_UNSPECIFIED = 0,

  JOIN_TYPE_INNER_JOIN = 1,

  JOIN_TYPE_LEFT_JOIN = 2,

  JOIN_TYPE_RIGHT_JOIN = 3,

  JOIN_TYPE_FULL_JOIN = 4,

  JOIN_TYPE_DIFFERENCE = 5,
};

enum class SourceType : uint8_t {
  SOURCE_TYPE_UNSPECIFIED = 0,

  // Local csv file.
  SOURCE_TYPE_FILE_CSV = 1,
};

struct ResultJoinConfig {
  ResultJoinType type = ResultJoinType::JOIN_TYPE_INNER_JOIN;

  // Required if advanced_join_type is ADVANCED_JOIN_TYPE_LEFT_JOIN or
  // ADVANCED_JOIN_TYPE_RIGHT_JOIN.
  uint32_t left_side_rank = 0;
};

// The input parameters of psi.
struct InputParams {
  SourceType type = SourceType::SOURCE_TYPE_FILE_CSV;

  // The path of input csv file.
  std::string path;

  std::vector<std::string> selected_keys;

  // Whether the intersection key value is unique in the input.
  // Note that if set to true but the key value is not unique, an unknown error
  // may result.
  // Default value: false.
  bool keys_unique = false;
};

// The output parameters of psi.
struct OutputParams {
  SourceType type = SourceType::SOURCE_TYPE_FILE_CSV;

  // The path of output csv file.
  std::string path;

  // Whether to sort output file by select fields.
  // It true, output is not promised to be aligned.
  bool disable_alignment = false;

  // Null representation in output csv file.
  // Default value: "NULL".
  std::string csv_null_rep = "NULL";
};

// Configuration for recovery checkpoint.
// If a PSI task failed unexpectedly, e.g. network failures and restart, the
// task can resume to the latest checkpoint to save time.
// However, enabling recovery would due in extra disk IOs and disk space
// occupation.
struct CheckpointConfig {
  bool enable = false;

  std::string path;
};

struct PsiExecuteConfig {
  // Configs for protocols.
  PsiProtocolConfig protocol_conf;

  // Configs for input.
  InputParams input_params;

  // Configs for output.
  OutputParams output_params;

  // Only two-party intersection scenarios are supported.
  ResultJoinConfig join_conf;

  CheckpointConfig checkpoint_conf;
};

}  // namespace psi::api
