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

#include <cmath>
#include <cstdint>
#include <vector>

#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/poly_matrix.h"

namespace psi::spiral {

constexpr uint64_t kNumWidths{4};

constexpr double PI{3.141592653589793};

struct DiscreteGaussian {
  std::vector<uint64_t> cdf_table_;
  int64_t max_val_ = 0;

  DiscreteGaussian() = default;
  explicit DiscreteGaussian(double noise_width);

  uint64_t Sample(uint64_t modulus, yacl::crypto::Prg<uint64_t>& prg) const;

  void SampleMatrix(const Params& params, PolyMatrixRaw& p,
                    yacl::crypto::Prg<uint64_t>& prg) const;
};

}  // namespace psi::spiral
