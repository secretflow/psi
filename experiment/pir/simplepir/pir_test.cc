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
    server = std::make_unique<pir::simple::SimplePirServer>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus);

    // Initialize PIR client with test parameters
    client = std::make_unique<pir::simple::SimplePirClient>(
        kTestDim, kTestModulus, kTestSize, kTestPlainModulus, 4, 6.8);

    // Generate test LWE matrix data for PIR
    GenerateLweMatrix();
  }

  void TearDown() override {
    // Clean up server and client resources
    server.reset();
    client.reset();
  }

  void GenerateLweMatrix() {
    A.resize(kTestDim);
    size_t row = static_cast<size_t>(sqrt(kTestSize));
    for (size_t i = 0; i < kTestDim; i++) {
      A[i] = pir::simple::GenerateRandomVector(row, kTestModulus);
    }
  }

  std::vector<std::vector<uint64_t>> A;
  std::unique_ptr<pir::simple::SimplePirServer> server;
  std::unique_ptr<pir::simple::SimplePirClient> client;
};

TEST_F(PIRTest, AllWorkflow) {
  // Phase 1: Set LWE matrix for server and client
  server->SetA_(A);
  server->GenerateDatabase();
  client->MatrixTranspose(A);

  // Phase 2: PIR setup
  auto lctxs = yacl::link::test::SetupWorld(2);
  auto server_setup = std::async([&] { server->ServerSetup(lctxs[0]); });
  auto client_setup = std::async([&] { client->ClientSetup(lctxs[1]); });
  server_setup.get();
  client_setup.get();

  // Phase 3: PIR query
  const size_t kTestIndex = 10;
  auto client_query =
      std::async([&] { client->ClientQuery(kTestIndex, lctxs[0]); });
  auto server_query = std::async([&] { server->ServerQuery(lctxs[1]); });
  client_query.get();
  server_query.get();

  // Phase 4: PIR answer
  auto server_answer = std::async([&] { server->ServerAnswer(lctxs[0]); });
  auto client_answer = std::async([&] { client->ClientAnswer(lctxs[1]); });
  server_answer.get();
  client_answer.get();

  // Phase 5: Result verification
  auto recovered = client->ClientRecover();
  auto expected = server->GetValue(kTestIndex);
  EXPECT_EQ(recovered, expected);
}
}  // namespace pir::simple
