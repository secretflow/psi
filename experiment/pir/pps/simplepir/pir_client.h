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
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "data_transmit.h"
#include "generate_rand.h"
#include "inner_product.h"

namespace pir::simple {
class PIRClient {
 public:
  PIRClient(size_t n, size_t q, size_t N, size_t p, int radius, double sigma,
            std::string ip, int port);

  void matrix_transpose_128(const std::vector<std::vector<__uint128_t>> &mat);

  void client_setup();

  void client_query(size_t idx);

  void client_answer();

  __uint128_t client_recover();

 private:
  size_t n_;                                    // dimension
  size_t q_;                                    // modulus
  size_t N_;                                    // database size
  size_t p_;                                    // plaintext modulus
  size_t delta_;                                // scalar
  size_t idx_row_;                              // row index
  std::vector<__uint128_t> s_;                  // secret vector
  std::vector<__uint128_t> ans_;                // answer vector
  std::vector<std::vector<__uint128_t>> hint_;  // hint from server
  std::vector<std::vector<__uint128_t>> A_;     // LWE matrix
  std::vector<double> gaussian_distribution_;   // Gaussian distribution
  std::string ip_;
  int port_;

  void precompute_discrete_gaussian(const int &radius, const double &sigma) {
    if (radius <= 0 || sigma <= 0) {
      throw std::invalid_argument("Invalid radius or sigma");
    }

    const double range = radius * sigma;
    const size_t max_k = static_cast<size_t>(floor(range));
    const double sigma_sq = sigma * sigma;
    double sum = 0.0;

    for (size_t k = -max_k; k <= max_k; k++) {
      double prob = exp(-k * k / (2 * sigma_sq));
      gaussian_distribution_.push_back(prob);
      sum += prob;
    }

    for (size_t i = 0; i < gaussian_distribution_.size(); i++) {
      gaussian_distribution_[i] /= sum;
    }
  }

  std::vector<size_t> sample_batch(size_t n) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<size_t> dist(gaussian_distribution_.begin(),
    gaussian_distribution_.end());

    std::vector<size_t> samples(n);
    const size_t max_k = (gaussian_distribution_.size() - 1) / 2;
    for (size_t i = 0; i < n; i++) {
      samples[i] = dist(gen) - max_k;
    }

    return samples;
  }
};
}  // namespace pir::simple

