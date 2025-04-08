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

#include "server.h"

#include <cmath>
#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

namespace pir::simple {
SimplePirServer::SimplePirServer(size_t dimension, uint64_t q, size_t N,
                                 uint64_t p)
    : dimension_(dimension), q_(q), N_(N), p_(p) {}

void SimplePirServer::GenerateDatabase() {
  size_t row = static_cast<size_t>(sqrt(N_));
  size_t col = static_cast<size_t>(sqrt(N_));
  database_.resize(row);
  for (size_t i = 0; i < row; i++) {
    // Generate row with random values in [0, p_)
    database_[i] = GenerateRandomVector(col, p_, true);
  }
}

void SimplePirServer::SetA_(const std::vector<std::vector<uint64_t>> &A) {
  YACL_ENFORCE(!A.empty());
  YACL_ENFORCE(A[0].size() == static_cast<size_t>(sqrt(N_)));
  YACL_ENFORCE(A.size() == dimension_);
  A_ = A;
}

// PIR setup phase:
// 1. Precomputes hint = db * A^T mod q
// 2. Sends hint to client through network
void SimplePirServer::ServerSetup(std::shared_ptr<yacl::link::Context> lctx) {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<uint64_t> hint;
  hint.reserve(row_num * dimension_);

  // Computes matrix product = db * A^T mod q
  // Stores as a vector
  for (size_t i = 0; i < row_num; i++) {
    for (size_t j = 0; j < dimension_; j++) {
      uint64_t prod = InnerProductModq(database_[i], A_[j], q_);
      hint.push_back(prod);
    }
  }

  // Sends precomputed hint to client
  SendData(hint, lctx);
}

// PIR query phase:
// 1. Receives and stores encrypted query from client
// 2. Uses blocking network receiver to wait for query data
void SimplePirServer::ServerQuery(std::shared_ptr<yacl::link::Context> lctx) {
  qu_ = RecvData(lctx);  // Store encrypted query vector
}

// PIR answer phase:
// 1. Calculates ans = db * qu mod q
// 2. Sends encrypted response back to client
void SimplePirServer::ServerAnswer(std::shared_ptr<yacl::link::Context> lctx) {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<uint64_t> ans(row_num);

  // Compute matrix-vector product with query
  for (size_t i = 0; i < row_num; i++) {
    ans[i] = InnerProductModq(database_[i], qu_, q_);
  }
  // Send response vector to client
  SendData(ans, lctx);
}

// Retrieves plaintext value from database by index
// @param idx: Linear index in [0, N_)
// @return: Plaintext value at position (row, col)
uint64_t SimplePirServer::GetValue(const size_t &idx) {
  if (idx >= N_) {
    SPDLOG_ERROR("Index out of bounds: {} >= {}", idx, N_);
  }
  size_t row_num = static_cast<size_t>(sqrt(N_));
  size_t row = idx / row_num;
  size_t col = idx % row_num;
  uint64_t value = database_[row][col];
  return value;
}
}  // namespace pir::simple
