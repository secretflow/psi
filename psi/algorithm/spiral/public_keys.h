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
#include <optional>
#include <utility>
#include <vector>

#include "yacl/base/int128.h"

#include "psi/algorithm/spiral/poly_matrix.h"

namespace psi::spiral {

// some public keys for server computation
struct PublicKeys {
  // packing key in Page 20
  std::vector<PolyMatrixNtt> v_packing_;

  // Automorphism keys for first dimension's encoding in Query, Similar to
  // GaolisKey in SealPIR
  std::vector<PolyMatrixNtt> v_expansion_left_;

  // Automorphism keys for subsequent dimension's encoding in Query
  std::vector<PolyMatrixNtt> v_expansion_right_;

  // conversion key for Regev to Gsw
  std::vector<PolyMatrixNtt> v_conversion_;

  PublicKeys() = default;

  PublicKeys(std::vector<PolyMatrixNtt>&& v_packing,
             std::vector<PolyMatrixNtt>&& v_expansion_left,
             std::vector<PolyMatrixNtt>&& v_expansion_right,
             std::vector<PolyMatrixNtt>&& v_conversion)
      : v_packing_(std::move(v_packing)),
        v_expansion_left_(std::move(v_expansion_left)),
        v_expansion_right_(std::move(v_expansion_right)),
        v_conversion_(std::move(v_conversion)) {}

  bool operator==(const PublicKeys& other) const {
    return this->v_packing_ == other.v_packing_ &&
           this->v_expansion_left_ == other.v_expansion_left_ &&
           this->v_expansion_right_ == other.v_expansion_right_;
  }
};

}  // namespace psi::spiral