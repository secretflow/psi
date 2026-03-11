// Copyright 2025 The secretflow authors.
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

#include "psi/algorithm/spiral/common.h"
#include "psi/algorithm/spiral/params.h"

namespace psi::ypir {

struct LWEParams {
  size_t n = 1024;
  uint64_t modulus = 1ULL << 32;
  uint64_t pt_modulus = 1ULL << 8;
  size_t q2_bits = 28;
  double noise_width = 27.57291103;

  uint64_t ScaleK() const { return modulus / pt_modulus; }
  static LWEParams Default() { return LWEParams(); }
  uint64_t GetQPrime2() const {
    const size_t modulus_bits =
        static_cast<size_t>(std::ceil(std::log2(static_cast<double>(modulus))));
    if (q2_bits == modulus_bits) {
      return modulus;
    } else {
      return psi::spiral::kQ2Values[q2_bits];
    }
  }
};

// Create parameters for SimplePIR scenario
psi::spiral::Params CreateParamsForScenarioSimplePIR(uint64_t num_items,
                                                     uint64_t item_size_bits);

// Create parameters for DoublePIR scenario
psi::spiral::Params CreateParamsForScenario(uint64_t num_items,
                                            uint64_t item_size_bits);

}  // namespace psi::ypir
