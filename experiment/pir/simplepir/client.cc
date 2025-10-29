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

#include "experiment/pir/simplepir/client.h"

#include <vector>

#include "yacl/crypto/tools/prg.h"

namespace pir::simple {
SimplePirClient::SimplePirClient(size_t dimension, uint64_t q, size_t N,
                                 uint64_t p, int radius, double sigma)
    : dimension_(dimension), q_(q), N_(N), p_(p) {
  // Checks if N is a perfect square
  YACL_ENFORCE(N > 0, "N must be positive");
  YACL_ENFORCE(N == static_cast<size_t>(sqrt(N)) * static_cast<size_t>(sqrt(N)),
               "N must be a perfect square");
  YACL_ENFORCE(dimension > 0, "Dimension must be positive");
  YACL_ENFORCE(q > 0, "Modulus q must be positive");
  YACL_ENFORCE(p > 0, "Modulus p must be positive");
  // Calculates scaling factor between plaintext and ciphertext spaces
  delta_ = static_cast<uint64_t>(
      floor(static_cast<double>(q) / static_cast<double>(p)));

  // Precomputes discrete Gaussian distribution for error sampling
  PrecomputeDiscreteGaussian(radius, sigma);
}

void SimplePirClient::Setup(uint128_t seed,
                            const std::vector<uint64_t> &hint_vec) {
  // Sets LWE matrix and Hint matrix
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  auto rand_vals =
      yacl::crypto::PrgAesCtr<uint64_t>(seed, dimension_ * row_num);

  A_.resize(row_num, std::vector<uint64_t>(dimension_));
  hint_.resize(row_num, std::vector<uint64_t>(dimension_));
  for (size_t i = 0; i < row_num; i++) {
    for (size_t j = 0; j < dimension_; j++) {
      A_[i][j] = rand_vals[j * row_num + i] % q_;
      hint_[i][j] = hint_vec[i * dimension_ + j];
    }
  }
}

std::vector<uint64_t> SimplePirClient::Query(size_t idx) {
  YACL_ENFORCE(idx < N_, "Index out of bounds: {}", idx);
  const size_t row_num = static_cast<size_t>(sqrt(N_));

  // Converts linear index to 2D coordinates
  idx_row_ = idx / row_num;
  size_t idx_col = idx % row_num;

  // Computes encrypted query: qu = A*s + e + delta*u_i_col (mod q)
  std::vector<uint64_t> u_i_col(row_num);
  u_i_col[idx_col] = delta_;

  std::vector<int> error_vec = SampleGaussian(row_num);

  s_ = GenerateRandomVector(dimension_, q_);
  std::vector<uint64_t> qu(row_num);
  for (size_t i = 0; i < row_num; i++) {
    qu[i] = InnerProductModq(A_[i], s_, q_) + u_i_col[i];
    qu[i] += static_cast<uint64_t>(error_vec[i]);
    qu[i] %= q_;
  }

  return qu;
}

uint64_t SimplePirClient::Recover(const std::vector<uint64_t> &ans) {
  // Computes d_ = ans - hint*s mod q
  uint64_t phase = ans[idx_row_] - InnerProductModq(hint_[idx_row_], s_, q_);

  // Handles modular rounding: Map from Z_q to Z_p
  uint64_t d =
      (phase % delta_ >= delta_ / 2) ? (phase / delta_) + 1 : (phase / delta_);
  return d % p_;  // Final plaintext in Z_p
}

void SimplePirClient::PrecomputeDiscreteGaussian(int radius, double sigma) {
  YACL_ENFORCE(radius > 0 && sigma > 0);

  const double range = radius * sigma;
  const int max_k = static_cast<int>(floor(range));
  const double sigma_sq = sigma * sigma;
  double sum = 0.0;

  // Computes unnormalized probabilities
  for (int k = -max_k; k <= max_k; k++) {
    double prob = exp(-k * k / (2 * sigma_sq));
    gaussian_distribution_.push_back(prob);
    sum += prob;
  }

  // Normalizes to create valid probability distribution
  for (size_t i = 0; i < gaussian_distribution_.size(); i++) {
    gaussian_distribution_[i] /= sum;
  }

  // Precomputes cumulative distribution function (CDF)
  // for sampling
  cumulative_distribution_.resize(gaussian_distribution_.size());
  cumulative_distribution_[0] = gaussian_distribution_[0];
  for (size_t i = 1; i < gaussian_distribution_.size(); i++) {
    cumulative_distribution_[i] =
        cumulative_distribution_[i - 1] + gaussian_distribution_[i];
  }
}

std::vector<int> SimplePirClient::SampleGaussian(size_t num_samples) {
  const int max_k = (gaussian_distribution_.size() - 1) / 2;
  std::vector<int> samples(num_samples);

  for (size_t i = 0; i < num_samples; ++i) {
    uint64_t rand_val = yacl::crypto::RandU64();
    constexpr double scale = 1.0 / (1ULL << 53);
    double u = static_cast<double>(rand_val >> 11) * scale;

    auto it = std::upper_bound(cumulative_distribution_.begin(),
                               cumulative_distribution_.end(), u);

    size_t bin_index = std::distance(cumulative_distribution_.begin(), it);
    samples[i] = static_cast<int>(bin_index) - max_k;
  }

  return samples;
}
}  // namespace pir::simple
