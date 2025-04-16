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

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "util.h"

namespace pir::simple {
class SimplePirClient {
 public:
  // Constructor initializes cryptographic parameters
  // @param dimension: LWE problem dimension
  // @param q: Ciphertext modulus
  // @param N: Total database size (must be square number)
  // @param p: Plaintext modulus
  // @param radius: Gaussian distribution range parameter
  // @param sigma: Gaussian distribution standard deviation
  SimplePirClient(size_t dimension, uint64_t q, size_t N, uint64_t p,
                  int radius, double sigma);

  // Setup phase: Receives and stores precomputed hint values from server
  // hint = database * A^T mod q
  void Setup(uint128_t seed, const std::vector<uint64_t> &hint_vec);

  // Query phase:
  // 1. Encodes target row index and column index
  // 2. Adds Gaussian noise for LWE security
  // 3. Sends encrypted query to server
  // @param idx: Linear index of target database element
  std::vector<uint64_t> Query(size_t idx);

  // Recover phase: Extracts plaintext from encrypted response
  // 1. Removes secret key component using hint
  // 2. Applies modulus switching (q → p)
  // @return Retrieved plaintext value from database
  uint64_t Recover(const std::vector<uint64_t> &ans);

 private:
  size_t dimension_ = 1024;                      // dimension
  uint64_t q_ = 1ULL << 32;                      // modulus
  size_t N_ = 0;                                 // database size
  uint64_t p_ = 0;                               // plaintext modulus
  uint64_t delta_ = 0;                           // scalar
  size_t idx_row_ = 0;                           // row index
  std::vector<uint64_t> s_;                      // secret vector
  std::vector<std::vector<uint64_t>> hint_;      // hint from server
  std::vector<std::vector<uint64_t>> A_;         // LWE matrix
  std::vector<double> gaussian_distribution_;    // Gaussian distribution
  std::vector<double> cumulative_distribution_;  // Cumulative distribution
                                                 // function (CDF) for sampling

  // Precomputes discrete Gaussian distribution for error sampling
  // @param radius: Number of standard deviations to consider (±range)
  // @param sigma: Standard deviation of distribution
  void PrecomputeDiscreteGaussian(int radius, double sigma);

  // Samples errors from precomputed distribution
  // @param n: Number of samples to generate
  // @return Vector of integer errors in [-radius*sigma, radius*sigma]
  std::vector<int> SampleGaussian(size_t n);
};
}  // namespace pir::simple
