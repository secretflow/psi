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
#include "yacl/crypto/tools/prg.h"

namespace pir::simple {
SimplePirServer::SimplePirServer(size_t dimension, uint64_t q, size_t N,
                                 uint64_t p)
    : dimension_(dimension), q_(q), N_(N), p_(p) {}

void SimplePirServer::SetDatabase(
    const std::vector<std::vector<uint64_t>> &database) {
  // Checks if database is empty
  if (database.empty()) {
    SPDLOG_ERROR("Database is empty");
    return;
  }

  // Checks if database size matches expected size
  size_t row_num = static_cast<size_t>(sqrt(N_));
  size_t col_num = static_cast<size_t>(sqrt(N_));
  if (database.size() != row_num || database[0].size() != col_num) {
    SPDLOG_ERROR("Database size mismatch: expected {} x {}, got {} x {}",
                 row_num, col_num, database.size(), database[0].size());
    return;
  }

  // Sets the database
  database_ = database;
}

void SimplePirServer::GenerateLweMatrix() {
  seed_ = yacl::crypto::SecureRandSeed();
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  auto rand_vals =
      yacl::crypto::PrgAesCtr<uint64_t>(seed_, dimension_ * row_num);

  A_.resize(dimension_, std::vector<uint64_t>(row_num));
  for (size_t i = 0; i < dimension_; i++) {
    for (size_t j = 0; j < row_num; j++) {
      A_[i][j] = rand_vals[i * row_num + j] % q_;
    }
  }
}

uint128_t SimplePirServer::GetSeed() const { return seed_; }

std::vector<uint64_t> SimplePirServer::Setup() {
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

  return hint;
}

std::vector<uint64_t> SimplePirServer::Answer(const std::vector<uint64_t> &qu) {
  const size_t row_num = static_cast<size_t>(sqrt(N_));
  std::vector<uint64_t> ans(row_num);

  // Computes matrix-vector product with query
  for (size_t i = 0; i < row_num; i++) {
    ans[i] = InnerProductModq(database_[i], qu, q_);
  }

  return ans;
}

uint64_t SimplePirServer::GetValue(size_t idx) {
  if (idx >= N_) {
    SPDLOG_ERROR("Index out of bounds: {} >= {}", idx, N_);
  }

  size_t row_num = static_cast<size_t>(sqrt(N_));
  size_t row_idx = idx / row_num;
  size_t col_idx = idx % row_num;
  uint64_t value = database_[row_idx][col_idx];

  return value;
}
}  // namespace pir::simple
