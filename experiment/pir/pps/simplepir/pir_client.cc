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

#include "pir_client.h"

#include <vector>

namespace pir::simple {

PIRClient::PIRClient(size_t n, size_t q, size_t N, size_t p, int radius,
                     double sigma, std::string ip, int port)
    : n_(n), q_(q), N_(N), p_(p), ip_(ip), port_(port) {
  delta_ = static_cast<size_t>(
      floor(static_cast<double>(q) / static_cast<double>(p)));
  precompute_discrete_gaussian(radius, sigma);
}

void PIRClient::matrix_transpose_128(
    const std::vector<std::vector<__uint128_t>> &mat) {
  if (mat.empty()) {
    throw std::invalid_argument("Empty matrix");
  }

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

void PIRClient::client_setup() {
  Receiver receiver(port_);
  std::vector<__uint128_t> hint_vec = receiver.receiveData();

  const size_t row_num = static_cast<size_t>(sqrt(N_));
  hint_.resize(row_num);

  for (size_t i = 0; i < row_num; i++) {
    hint_[i].resize(n_);
    for (size_t j = 0; j < n_; j++) {
      hint_[i][j] = hint_vec[i * n_ + j];
    }
  }
}

void PIRClient::client_query(size_t idx) {
  size_t row_num = static_cast<size_t>(sqrt(N_));
  idx_row_ = idx / row_num;
  size_t idx_col = idx % row_num;
  std::vector<size_t> u_i_col(row_num);
  u_i_col[idx_col] = delta_;

  std::vector<size_t> error_vec = sample_batch(row_num);

  s_ = generate_random_vector(n_, q_);
  std::vector<__uint128_t> qu;
  qu.resize(row_num);
  for (size_t i = 0; i < row_num; i++) {
    qu[i] = (fast_inner_product_modq(A_[i], s_, q_) +
             static_cast<__uint128_t>(u_i_col[i]));
    qu[i] += static_cast<__uint128_t>(error_vec[i]);
    qu[i] %= q_;
  }

  Sender sender(ip_, port_);
  sender.sendData(qu);
}

void PIRClient::client_answer() {
  Receiver receiver(port_);
  ans_ = receiver.receiveData();
}

__uint128_t PIRClient::client_recover() {
  __uint128_t d_ =
      ans_[idx_row_] - fast_inner_product_modq(hint_[idx_row_], s_, q_);
  __uint128_t d = static_cast<__uint128_t>(
      (d_ % delta_ >= delta_ / 2) ? (d_ / delta_) - 1 : (d_ / delta_));
  return d % p_;
}
}  // namespace pir::simple

