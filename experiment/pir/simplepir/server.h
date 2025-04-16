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

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "experiment/pir/simplepir/util.h"

namespace pir::simple {
class SimplePirServer {
 public:
  // Constructor for PIR server
  // @param dimension - Dimension of the LWE problem (security parameter)
  // @param q - Cryptographic modulus
  // @param N - Total number of elements in the database
  // @param p - Plaintext modulus
  SimplePirServer(size_t dimension, uint64_t q, size_t N, uint64_t p);

  // Database is organized as sqrt(N) x sqrt(N) matrix for efficient processing
  void SetDatabase(const std::vector<std::vector<uint64_t>> &database);

  // Generates the n x sqrt(N) LWE matrix (column-major format) used for
  // cryptographic operations
  void GenerateLweMatrix();

  // Gets the CSPRNG seed used to generate LWE matrix
  uint128_t GetSeed() const;

  // Precomputes hint = db * A^T mod q
  std::vector<uint64_t> GetHint() const;

  // PIR answer phase:
  // 1. Calculates ans = db * qu mod q
  // 2. Sends encrypted response back to client
  std::vector<uint64_t> Answer(const std::vector<uint64_t> &qu);

  // Retrieves plaintext value from database
  // @param idx - Index of requested data element
  // @return Plaintext value at specified index
  uint64_t GetValue(size_t idx);

 private:
  size_t dimension_ = 1024;  //  dimension
  uint64_t q_ = 1ULL << 32;  //  modulus
  size_t N_ = 0;             //  database size
  uint64_t p_ = 0;           //  plaintext modulus
  uint128_t seed_ = 0;       //  seed for random number generation
  std::vector<std::vector<uint64_t>> database_;  //  database
  std::vector<std::vector<uint64_t>> A_;         //  LWE matrix
};
}  // namespace pir::simple
