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

#include <future>
#include <vector>

#include "experiment/pir/simplepir/client.h"
#include "experiment/pir/simplepir/network_util.h"
#include "experiment/pir/simplepir/server.h"
#include "yacl/link/test_util.h"

namespace pir::simple {
constexpr size_t kTestDim = 1 << 10;
constexpr size_t kTestSize = 1ULL << 12;
constexpr uint64_t kTestModulus = 1ULL << 32;
constexpr uint64_t kTestPlainModulus = 991;
// const size_t kTestIndex = 10;

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
  pir::simple::SimplePirServer server(kTestDim, kTestModulus, kTestSize,
                                      kTestPlainModulus);
  pir::simple::SimplePirClient client(kTestDim, kTestModulus, kTestSize,
                                      kTestPlainModulus, 4, 6.8);
  std::vector<std::vector<uint64_t>> database;
  GenerateDatabase(database);
  // Phase 1: Sets LWE matrix for server and client
  server.SetDatabase(database);
  server.GenerateLweMatrix();
  uint128_t server_seed = server.GetSeed();
  auto server_hint_vec = server.GetHint();

  // Phase 2: PIR setup
  // Sends LWE matrix for client
  uint128_t client_seed;
  auto lctxs = yacl::link::test::SetupWorld(2);
  auto sender = std::async([&] { SendUint128(server_seed, lctxs[0]); });
  auto receiver = std::async([&] { RecvUint128(client_seed, lctxs[1]); });
  sender.get();
  receiver.get();

  // Sends hint to client
  std::vector<uint64_t> client_hint_vec;
  sender = std::async([&] { SendVector(server_hint_vec, lctxs[0]); });
  receiver = std::async([&] { RecvVector(client_hint_vec, lctxs[1]); });
  sender.get();
  receiver.get();

  client.Setup(client_seed, client_hint_vec);

  // Phase 3: PIR query
  const size_t kTestIndex = 10;
  auto client_query = client.Query(kTestIndex);
  std::vector<uint64_t> server_query_received;

  sender = std::async([&] { SendVector(client_query, lctxs[0]); });
  receiver = std::async([&] { RecvVector(server_query_received, lctxs[1]); });
  sender.get();
  receiver.get();

  // Phase 4: PIR answer
  auto server_answer = server.Answer(server_query_received);
  std::vector<uint64_t> client_answer_received;

  sender = std::async([&] { SendVector(server_answer, lctxs[0]); });
  receiver = std::async([&] { RecvVector(client_answer_received, lctxs[1]); });
  sender.get();
  receiver.get();

  // Phase 5: Result verification
  auto recovered = client.Recover(client_answer_received);
  auto expected = server.GetValue(kTestIndex);
  EXPECT_EQ(recovered, expected);
}
}  // namespace pir::simple
