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

#include "pir_server.h"

#include <cmath>
#include <string>
#include <vector>

namespace pir::simple {
PIRServer::PIRServer(size_t n, size_t q, size_t N, size_t p, std::string ip,
                     int port)
    : n_(n), q_(q), N_(N), p_(p), ip_(ip), port_(port) {}

void PIRServer::generate_database() {
  size_t row = static_cast<size_t>(sqrt(N_));
  size_t col = static_cast<size_t>(sqrt(N_));
  database_.resize(row);
  for (size_t i = 0; i < row; i++) {
    database_[i] = generate_random_vector(col, p_);
  }
}

void PIRServer::set_A_(const std::vector<std::vector<__uint128_t>> &A) {
  A_ = A;
}

void PIRServer::server_setup() {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<__uint128_t> hint;
  hint.reserve(row_num * n_);
  for (size_t i = 0; i < row_num; i++) {
    for (size_t j = 0; j < n_; j++) {
      __uint128_t prod = fast_inner_product_modq(database_[i], A_[j], q_);
      hint.push_back(prod);
    }
  }
  Sender sender(ip_, port_);
  sender.sendData(hint);
}

void PIRServer::server_query() {
  Receiver receiver(port_);
  qu_ = receiver.receiveData();
}

void PIRServer::server_answer() {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<__uint128_t> ans(row_num);
  for (size_t i = 0; i < row_num; i++) {
    ans[i] = fast_inner_product_modq(database_[i], qu_, q_);
  }
  Sender sender(ip_, port_);
  sender.sendData(ans);
}

__uint128_t PIRServer::get_value(const size_t &idx) {
  size_t row_num = static_cast<size_t>(sqrt(N_));
  size_t row = idx / row_num;
  size_t col = idx % row_num;
  __uint128_t value = database_[row][col];
  return value;
}
}  // namespace pir::simple
