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
#include <memory>
#include <vector>

#include "client.h"
#include "server.h"
#include "yacl/link/test_util.h"

namespace pir::simple {
constexpr size_t kTestDim = 1 << 10;
constexpr size_t kTestSize = 1ULL << 12;
constexpr uint64_t kTestModulus = 1ULL << 32;
constexpr uint64_t kTestPlainModulus = 991;
const size_t kTestIndex = 10;

class PIRTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize PIR server with test parameters
    server = std::make_unique<pir::simple::PIRServer>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus);

    // Initialize PIR client with test parameters
    client = std::make_unique<pir::simple::PIRClient>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus, 4, 6.8);

    // Generate test LWE matrix data for PIR
    generate_LWE_matrix();
  }

  void TearDown() override {
    // Clean up server and client resources
    server.reset();
    client.reset();
  }

  void generate_LWE_matrix() {
    A.resize(kTestDim);
    size_t row = static_cast<size_t>(sqrt(kTestSize));
    for (size_t i = 0; i < kTestDim; i++) {
      A[i] = pir::simple::generate_random_vector(row, kTestModulus);
    }
  }

  std::vector<std::vector<uint64_t>> A;
  std::unique_ptr<pir::simple::PIRServer> server;
  std::unique_ptr<pir::simple::PIRClient> client;
};

TEST_F(PIRTest, AllWorkflow) {
  // Phase 1: Set LWE matrix for server and client
  server->set_A_(A);
  server->generate_database();
  client->matrix_transpose(A);

  // Phase 2: PIR setup
  auto lctxs = yacl::link::test::SetupWorld(2);
  auto server_setup = std::async([&] { server->server_setup(lctxs[0]); });
  auto client_setup = std::async([&] { client->client_setup(lctxs[1]); });
  server_setup.get();
  client_setup.get();

  // Phase 3: PIR query
  const size_t kTestIndex = 10;
  auto client_query =
      std::async([&] { client->client_query(kTestIndex, lctxs[0]); });
  auto server_query = std::async([&] { server->server_query(lctxs[1]); });
  client_query.get();
  server_query.get();

  // Phase 4: PIR answer
  auto server_answer = std::async([&] { server->server_answer(lctxs[0]); });
  auto client_answer = std::async([&] { client->client_answer(lctxs[1]); });
  server_answer.get();
  client_answer.get();

  // Phase 5: Result verification
  auto recovered = client->client_recover();      // Client decrypts result
  auto expected = server->get_value(kTestIndex);  // Server's true value
  EXPECT_EQ(recovered, expected);  // Validate PIR protocol correctness
}
}  // namespace pir::simple
