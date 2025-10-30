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

#include <gtest/gtest.h>

#include "psi/algorithm/ypir/server.h"
#include "yacl/link/test_util.h"

namespace pir::ypir {

inline void GenerateDatabase(std::vector<std::vector<uint64_t>> &database) {
  size_t row = static_cast<size_t>(sqrt(kTestSize));
  size_t col = static_cast<size_t>(sqrt(kTestSize));
  database.resize(row);
  for (size_t i = 0; i < row; i++) {
    database[i] =
        pir::simple::GenerateRandomVector(col, kTestPlainModulus, true);
  }
}

TEST(PIRTest, AllWorkflow) {
  pir::ypir::YPIRServer server(1 << 6, 1 << 6, 1 << 10, 1 << 11, 65537, 1 << 32, 1 << 56, 12345);
  std::vector<std::vector<uint64_t>> database;
  GenerateDatabase(database);
  // Phase 1: Sets LWE matrix for server and client
  server.SetDatabase(database);
  server.GenerateLweMatrix();

  uint128_t server_seed = server.GetSeed();
  auto server_hint_vec = server.GetHint();

  client.Setup(server_seed, server_hint_vec);
}

}  // namespace pir::ypir