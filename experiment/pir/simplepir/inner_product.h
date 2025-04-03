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

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "yacl/base/int128.h"

namespace pir::simple {

/**
 * Computes the modular inner product of two vectors efficiently.
 *
 * @param row First vector (typically a row from a matrix)
 * @param col Second vector (typically a column from a matrix)
 * @param q Modulus value (must be > 1)
 *
 * @return (Σ(row[i] * col[i])) mod q
 */
uint64_t fast_inner_product_modq(const std::vector<uint64_t> &row,
                                 const std::vector<uint64_t> &col,
                                 const uint64_t &q);
}  // namespace pir::simple
