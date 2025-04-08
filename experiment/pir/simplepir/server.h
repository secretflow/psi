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
#include <memory>
#include <string>
#include <vector>

#include "network_util.h"
#include "util.h"

namespace pir::simple {
class SimplePirServer {
 public:
  // Constructor for PIR server
  // @param dimension - Dimension of the LWE problem (security parameter)
  // @param q - Cryptographic modulus
  // @param N - Total number of elements in the database
  // @param p - Plaintext modulus
  // @param ip - Network interface address for communication
  // @param port - Network port for client-server communication
  SimplePirServer(size_t dimension, uint64_t q, size_t N, uint64_t p);

  // Initializes database structure with random plaintext values
  // Database is organized as sqrt(N) x sqrt(N) matrix for efficient processing
  void GenerateDatabase();

  // Sets the n x sqrt(N) LWE matrix used for cryptographic operations
  // @param A - LWE matrix (column-major format)
  void SetA_(const std::vector<std::vector<uint64_t>> &A);

  void Setup(std::shared_ptr<yacl::link::Context> lctx);

  void Query(std::shared_ptr<yacl::link::Context> lctx);

  void Answer(std::shared_ptr<yacl::link::Context> lctx);

  // Retrieves plaintext value from database
  // @param idx - Index of requested data element
  // @return Plaintext value at specified index
  uint64_t GetValue(const size_t &idx);

 private:
  size_t dimension_ = 1024;                      //  dimension
  uint64_t q_ = 1ULL << 32;                      //  modulus
  size_t N_ = 0;                                 //  database size
  uint64_t p_ = 991;                             //  plaintext modulus
  std::vector<std::vector<uint64_t>> database_;  //  database
  std::vector<std::vector<uint64_t>> A_;         //  LWE matrix
  std::vector<uint64_t> qu_;                     //  query
};
}  // namespace pir::simple
