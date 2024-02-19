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

#include "psi/utils/recovery.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::kkrt {

// For KkrtOt
constexpr size_t kDefaultNumOt = 512;

void CommonInit(const std::string& key_hash_digest, v2::PsiConfig* config,
                RecoveryManager* recovery_manager);

}  // namespace psi::kkrt
