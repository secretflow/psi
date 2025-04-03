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

#include "client.h"

#include <vector>

namespace pir::simple {
PIRClient::PIRClient(size_t dimension,
                     uint64_t q,
                     size_t N,
                     uint64_t p,
                     int radius,
                     double sigma)
    : dimension_(dimension), q_(q), N_(N), p_(p) {
  // Calculate scaling factor between plaintext and ciphertext spaces
  delta_ = static_cast<size_t>(
      floor(static_cast<double>(q) / static_cast<double>(p)));

  // Precompute discrete Gaussian distribution for error sampling
  precompute_discrete_gaussian(radius, sigma);
}

void PIRClient::matrix_transpose(
    const std::vector<std::vector<uint64_t>> &mat) {
  YACL_ENFORCE(!mat.empty());

  const size_t row = mat.size();
  const size_t col = mat[0].size();

  A_.resize(col);
  for (size_t i = 0; i < col; i++) {
    A_[i].resize(row);
  }

  for (size_t i = 0; i < row; i++) {
    for (size_t j = 0; j < col; j++) {
      A_[j][i] = mat[i][j];
    }
  }
}

// Setup phase: Receives and stores precomputed hint values from server
// hint = database * A^T mod q
void PIRClient::client_setup(std::shared_ptr<yacl::link::Context> lctx) {
  std::vector<uint64_t> hint_vec = RecvData(lctx);
  // Reconstruct 2D hint matrix from linear vecto
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  hint_.resize(row_num);
  for (size_t i = 0; i < row_num; i++) {
    hint_[i].resize(dimension_);
    for (size_t j = 0; j < dimension_; j++) {
      hint_[i][j] = hint_vec[i * dimension_ + j];
    }
  }
}

// Query phase:
// 1. Encodes target row index and column index
// 2. Adds Gaussian noise for LWE security
// 3. Sends encrypted query to server
// @param idx: Linear index of target database element
void PIRClient::client_query(size_t idx,
                             std::shared_ptr<yacl::link::Context> lctx) {
  size_t row_num = static_cast<size_t>(sqrt(N_));

  // Convert linear index to 2D coordinates
  idx_row_ = idx / row_num;        // Target row index
  size_t idx_col = idx % row_num;  // Target column index

  // Compute encrypted query: qu = A*s + e + delta*u_i_col (mod q)
  std::vector<uint64_t> u_i_col(row_num);
  u_i_col[idx_col] = static_cast<uint64_t>(delta_);

  std::vector<size_t> error_vec = sample_batch(row_num);

  s_ = generate_random_vector(dimension_, q_);
  std::vector<uint64_t> qu;
  qu.resize(row_num);
  for (size_t i = 0; i < row_num; i++) {
    qu[i] = fast_inner_product_modq(A_[i], s_, q_) + u_i_col[i];
    qu[i] += static_cast<uint64_t>(error_vec[i]);
    qu[i] %= q_;
  }

  // Send encrypted query to server
  SendData(qu, lctx);
}

// Answer phase
// ans = db * qu mod q
void PIRClient::client_answer(std::shared_ptr<yacl::link::Context> lctx) {
  ans_ = RecvData(lctx);
}

// Recover phase: Extracts plaintext from encrypted response
// 1. Removes secret key component using hint
// 2. Applies modulus switching (q → p)
// @return Retrieved plaintext value from database
uint64_t PIRClient::client_recover() {
  // Compute d_ = ans - hint*s mod q
  uint64_t d_ =
      ans_[idx_row_] - fast_inner_product_modq(hint_[idx_row_], s_, q_);

  // Handle modular rounding: Map from Z_q to Z_p
  uint64_t d = (d_ % delta_ >= delta_ / 2) ? (d_ / delta_) - 1 : (d_ / delta_);
  return d % p_;  // Final plaintext in Z_p
}
}  // namespace pir::simple
