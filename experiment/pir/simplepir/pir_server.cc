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
PIRServer::PIRServer(size_t dimension, uint64_t q, size_t N, uint64_t p,
                     std::string ip, int port)
    : dimension_(dimension), q_(q), N_(N), p_(p), ip_(ip), port_(port) {}

void PIRServer::generate_database() {
  size_t row = static_cast<size_t>(sqrt(N_));
  size_t col = static_cast<size_t>(sqrt(N_));
  database_.resize(row);
  for (size_t i = 0; i < row; i++) {
    // Generate row with random values in [0, p_)
    database_[i] = generate_random_vector(col, p_, true);
  }
}

void PIRServer::set_A_(const std::vector<std::vector<uint64_t>> &A) { A_ = A; }

// PIR setup phase:
// 1. Precomputes hint = db * A^T mod q
// 2. Sends hint to client through network
void PIRServer::server_setup() {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<uint64_t> hint;
  hint.reserve(row_num * dimension_);

  // Computes matrix product = db * A^T mod q
  // Stores as a vector
  for (size_t i = 0; i < row_num; i++) {
    for (size_t j = 0; j < dimension_; j++) {
      uint64_t prod = fast_inner_product_modq(database_[i], A_[j], q_);
      hint.push_back(prod);
    }
  }

  // Sends precomputed hint to client
  Sender sender(ip_, port_);
  sender.sendData(hint);
}

// PIR query phase:
// 1. Receives and stores encrypted query from client
// 2. Uses blocking network receiver to wait for query data
void PIRServer::server_query() {
  Receiver receiver(port_);
  qu_ = receiver.receiveData();  // Store encrypted query vector
  receiver.~Receiver();          // Explicitly close receiver
}

// PIR answer phase:
// 1. Calculates ans = db * qu mod q
// 2. Sends encrypted response back to client
void PIRServer::server_answer() {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<uint64_t> ans(row_num);

  // Compute matrix-vector product with query
  for (size_t i = 0; i < row_num; i++) {
    ans[i] = fast_inner_product_modq(database_[i], qu_, q_);
  }
  // Send response vector to client
  Sender sender(ip_, port_);
  sender.sendData(ans);
}

// Retrieves plaintext value from database by index
// @param idx: Linear index in [0, N_)
// @return: Plaintext value at position (row, col)
uint64_t PIRServer::get_value(const size_t &idx) {
  size_t row_num = static_cast<size_t>(sqrt(N_));
  size_t row = idx / row_num;
  size_t col = idx % row_num;
  uint64_t value = database_[row][col];
  return value;
}
}  // namespace pir::simple
