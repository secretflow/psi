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

#include <cstdint>

#include "psi/config/psi.h"

namespace psi::api::ub {

enum class UbPsiRole : uint8_t {
  ROLE_UNSPECIFIED = 0,

  // server
  // In 2P unbalanced PSI, servers own a much larger dataset.
  ROLE_SERVER = 3,

  // server
  // In 2P unbalanced PSI, clients own a much smaller dataset.
  ROLE_CLIENT = 4,
};

struct UbPsiServerParams {
  // Required for MODE_OFFLINE_GEN_CACHE, MODE_OFFLINE, MODE_ONLINE and
  // MODE_FULL.
  std::string secret_key_path;
};

struct UbPsiExecuteConfig {
  enum class Mode : uint8_t {
    MODE_UNSPECIFIED = 0,

    // Servers generate cache only. First part of offline stage.
    MODE_OFFLINE_GEN_CACHE = 1,

    // Servers send cache to clients only. Second part of offline stage.
    MODE_OFFLINE_TRANSFER_CACHE = 2,

    // Run offline stage.
    MODE_OFFLINE = 3,

    // Run online stage.
    MODE_ONLINE = 4,

    // Run all stages.
    MODE_FULL = 5,
  };

  // Required.
  Mode mode = Mode::MODE_UNSPECIFIED;

  UbPsiRole role = UbPsiRole::ROLE_UNSPECIFIED;

  bool recevie_result = false;

  std::string cache_path;

  InputParams input_params;

  OutputParams output_params;

  UbPsiServerParams server_params;

  ResultJoinConfig join_conf;
};

}  // namespace psi::api::ub
