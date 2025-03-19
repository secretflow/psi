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

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "/root/SimplePIR/data_transmit.h"
#include "/root/SimplePIR/generate_rand.h"
#include "/root/SimplePIR/inner_product.h"

namespace pir::simple {
class PIRServer {
 public:
  PIRServer(size_t n, size_t q, size_t N, size_t p, std::string ip, int port);

  void set_A_(const std::vector<std::vector<__uint128_t>> &A);

  void write_qu(std::vector<__uint128_t> &&qu);

  void server_setup();

  void server_query();

  void server_answer();

 private:
  size_t n_;                                        // dimension
  size_t q_;                                        // modulus
  size_t N_;                                        // database size
  size_t p_;                                        // plaintext modulus
  std::vector<std::vector<__uint128_t>> database_;  // database
  std::vector<std::vector<__uint128_t>> A_;         // LWE matrix
  std::vector<__uint128_t> qu_;                     // query
  std::string ip_;
  int port_;
};
}  // namespace pir::simple
