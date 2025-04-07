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

#include "generate_rand.h"
#include "inner_product.h"
#include "receiver.h"
#include "sender.h"
#include "yacl/base/exception.h"

namespace pir::simple {
class PIRClient {
 public:
  // Constructor initializes cryptographic parameters
  // @param dimension: LWE problem dimension
  // @param q: Ciphertext modulus
  // @param N: Total database size (must be square number)
  // @param p: Plaintext modulus
  // @param radius: Gaussian distribution range parameter
  // @param sigma: Gaussian distribution standard deviation
  // @param ip: Server IP address for network communication
  // @param port: Server port number for network communication
  PIRClient(size_t dimension, uint64_t q, size_t N, uint64_t p, int radius,
            double sigma);

  // Transposes LWE matrix A for efficient computation
  // @param mat: LWE matrix from server (column-major format)
  void matrix_transpose(const std::vector<std::vector<uint64_t>> &mat);

  void client_setup(std::shared_ptr<yacl::link::Context> lctx);

  void client_query(size_t idx, std::shared_ptr<yacl::link::Context> lctx);

  void client_answer(std::shared_ptr<yacl::link::Context> lctx);

  uint64_t client_recover();

 private:
  size_t dimension_ = 1024;                    // dimension
  uint64_t q_ = 1ULL << 32;                    // modulus
  size_t N_ = 0;                               // database size
  uint64_t p_ = 991;                           // plaintext modulus
  uint64_t delta_ = q_ / p_;                   // scalar
  size_t idx_row_ = 0;                         // row index
  std::vector<uint64_t> s_;                    // secret vector
  std::vector<uint64_t> ans_;                  // answer vector
  std::vector<std::vector<uint64_t>> hint_;    // hint from server
  std::vector<std::vector<uint64_t>> A_;       // LWE matrix
  std::vector<double> gaussian_distribution_;  // Gaussian distribution

  // Precomputes discrete Gaussian distribution for error sampling
  // @param radius: Number of standard deviations to consider (±range)
  // @param sigma: Standard deviation of distribution
  void precompute_discrete_gaussian(const int &radius, const double &sigma) {
    YACL_ENFORCE(radius > 0 && sigma > 0);

    const double range = radius * sigma;
    const size_t max_k = static_cast<size_t>(floor(range));
    const double sigma_sq = sigma * sigma;
    double sum = 0.0;

    // Compute unnormalized probabilities
    for (size_t k = -max_k; k <= max_k; k++) {
      double prob = exp(-k * k / (2 * sigma_sq));
      gaussian_distribution_.push_back(prob);
      sum += prob;
    }

    // Normalize to create valid probability distribution
    for (size_t i = 0; i < gaussian_distribution_.size(); i++) {
      gaussian_distribution_[i] /= sum;
    }
  }

  // Samples errors from precomputed distribution
  // @param n: Number of samples to generate
  // @return Vector of integer errors in [-radius*sigma, radius*sigma]
  std::vector<size_t> sample_batch(size_t n) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<size_t> dist(gaussian_distribution_.begin(),
                                            gaussian_distribution_.end());

    std::vector<size_t> samples(n);
    const size_t max_k = (gaussian_distribution_.size() - 1) / 2;

    // Generate samples with center adjustment
    for (size_t i = 0; i < n; i++) {
      samples[i] = dist(gen) - max_k;  // Shift to symmetric range
    }

    return samples;
  }
};
}  // namespace pir::simple
