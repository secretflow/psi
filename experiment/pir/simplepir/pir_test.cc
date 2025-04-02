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

#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "pir_client.h"
#include "pir_server.h"

namespace pir::simple {
constexpr size_t kTestDim = 1 << 10;
constexpr size_t kTestSize = 1ULL << 12;
constexpr uint64_t kTestModulus = 1ULL << 32;
constexpr uint64_t kTestPlainModulus = 991;
constexpr int kTestPort = 12345;
const size_t kTestIndex = 10;

class PIRTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize PIR server with test parameters
    server = std::make_unique<pir::simple::PIRServer>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus, "127.0.0.1",
        kTestPort);

    // Initialize PIR client with test parameters
    client = std::make_unique<pir::simple::PIRClient>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus, 4, 6.8,
        "127.0.0.1", kTestPort);

    // Generate test LWE matrix data for PIR
    generate_test_matrix();
  }

  void TearDown() override {
    // Clean up server and client resources
    server.reset();
    client.reset();
  }

  void generate_test_matrix() {
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
  std::thread server_setup([this]() { server->server_setup(); });
  std::thread client_setup([this]() { client->client_setup(); });
  server_setup.join();
  client_setup.join();

  // Phase 3: PIR query
  const size_t kTestIndex = 10;
  std::thread server_query([this]() { server->server_query(); });
  std::thread client_query(
      [this, kTestIndex]() { client->client_query(kTestIndex); });
  server_query.join();
  client_query.join();

  // Phase 4: PIR answer
  std::thread server_answer([this]() { server->server_answer(); });
  std::thread client_answer([this]() { client->client_answer(); });
  server_answer.join();
  client_answer.join();

  // Phase 5: Result verification
  auto recovered = client->client_recover();      // Client decrypts result
  auto expected = server->get_value(kTestIndex);  // Server's true value
  EXPECT_EQ(recovered, expected);  // Validate PIR protocol correctness
}
}  // namespace pir::simple
