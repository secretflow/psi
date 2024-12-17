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
#include <memory>
#include <string>
#include <vector>

#include "psi/cryptor/ecc_cryptor.h"

namespace psi::ecdh {

enum EcdhStage { MaskSelf, MaskPeer, RecvDualMaskedSelf };

// EcdhLogger is for internal debug purposes.
class EcdhLogger {
 public:
  virtual ~EcdhLogger() = default;
  EcdhLogger() = default;

  // For RecvDualMaskedSelf, output should be left empty.
  virtual void Log(EcdhStage stage,
                   const std::array<uint8_t, kEccKeySize>& secret_key,
                   size_t start_idx, const std::vector<std::string>& input,
                   const std::vector<std::string>& output = {}) = 0;
};

}  // namespace psi::ecdh
