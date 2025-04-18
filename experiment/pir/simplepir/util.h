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
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/crypto/rand/rand.h"

namespace pir::simple {
/**
 * Generates a vector of cryptographically secure random numbers modulo q
 *
 * @param size        Number of elements in output vector (must be >0)
 * @param modulus    Modulus value for elements (must be >1)
 * @param fast_mode   When true, uses faster but non-cryptographic PRG
 *                    (default: false for security-sensitive contexts)
 *
 * @return Vector of random integers in [0, modulus_-1]
 *
 * @throws yacl::Exception if modulus_ < 2 or size == 0
 *
 * Features:
 * - Two PRG modes:
 *   1. Secure mode (default): Uses CSPRNG
 *   2. Fast mode: Uses faster but non-cryptographic PRG
 */
std::vector<uint64_t> GenerateRandomVector(size_t size, uint64_t modulus,
                                           bool fast_mode = false);

/**
 * Computes the modular inner product of two vectors efficiently.
 *
 * @param row First vector (typically a row from a matrix)
 * @param col Second vector (typically a column from a matrix)
 * @param q Modulus value (must be > 1)
 *
 * @return (Σ(row[i] * col[i])) mod q
 */
uint64_t InnerProductModq(const std::vector<uint64_t> &row,
                          const std::vector<uint64_t> &col, uint64_t q);
}  // namespace pir::simple
