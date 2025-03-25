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

#include <memory>
#include <thread>
#include <vector>

#include "pir_client.h"
#include "pir_server.h"

namespace pir::simple {
constexpr size_t TEST_N = 1 << 10;
constexpr size_t TEST_DB_SIZE = 1ULL << 12;
constexpr size_t TEST_Q = 1ULL << 32;
constexpr size_t TEST_P = 991;
constexpr int TEST_PORT = 12345;
const size_t TEST_IDX = 10;

class PIRTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server = std::make_unique<pir::simple::PIRServer>(
        TEST_N, TEST_Q, TEST_DB_SIZE, TEST_P, "127.0.0.1", TEST_PORT);

    client = std::make_unique<pir::simple::PIRClient>(
        TEST_N, TEST_Q, TEST_DB_SIZE, TEST_P, 4, 6.8, "127.0.0.1", TEST_PORT);

    generate_test_matrix();
  }

  void TearDown() override {
    server.reset();
    client.reset();
  }

  void generate_test_matrix() {
    A.resize(TEST_N);
    size_t row = static_cast<size_t>(sqrt(TEST_DB_SIZE));
    for (size_t i = 0; i < TEST_N; i++) {
      A[i] = pir::simple::generate_random_vector(row, TEST_Q);
    }
  }

  std::vector<std::vector<__uint128_t>> A;
  std::unique_ptr<pir::simple::PIRServer> server;
  std::unique_ptr<pir::simple::PIRClient> client;
};

TEST_F(PIRTest, AllWorkflow) {
  server->set_A_(A);
  server->generate_database();
  client->matrix_transpose_128(A);

  std::thread server_setup([this]() { server->server_setup(); });
  std::thread client_setup([this]() { client->client_setup(); });
  server_setup.join();
  client_setup.join();

  const size_t test_idx = 10;
  std::thread server_query([this]() { server->server_query(); });
  std::thread client_query(
      [this, test_idx]() { client->client_query(test_idx); });
  server_query.join();
  client_query.join();

  std::thread server_answer([this]() { server->server_answer(); });
  std::thread client_answer([this]() { client->client_answer(); });
  server_answer.join();
  client_answer.join();

  auto recovered = client->client_recover();
  auto expected = server->get_value(test_idx);
  EXPECT_EQ(recovered, expected);
}
}  // namespace pir::simple
