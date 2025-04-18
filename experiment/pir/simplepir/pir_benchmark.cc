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

#include <future>
#include <memory>
#include <vector>

#include "benchmark/benchmark.h"
#include "experiment/pir/simplepir/client.h"
#include "experiment/pir/simplepir/network_util.h"
#include "experiment/pir/simplepir/server.h"
#include "yacl/link/test_util.h"

namespace {
struct TestContext {
  size_t dimension = 1 << 10;
  size_t N = 1ULL << 12;
  uint64_t q = 1ULL << 32;
  uint64_t p = 991;
  int radius = 4;
  double sigma = 6.8;
  std::vector<std::vector<uint64_t>> database;
};

// Initializes test context with parameters
// 1. Generates random database
// 2. Configures parameters for lattice-based PIR scheme
static TestContext SetupContext() {
  TestContext ctx;
  size_t row = static_cast<size_t>(sqrt(ctx.N));
  ctx.database.resize(row);
  for (size_t i = 0; i < row; i++) {
    ctx.database[i] = pir::simple::GenerateRandomVector(row, ctx.p, true);
  }
  return ctx;
}

// Google Benchmark test case for PIR protocol
// Measures end-to-end execution time of PIR workflow:
// 1. Setup phase: Key exchange and precomputation
// 2. Query phase: Encrypted index retrieval
// 3. Answer phase: Database response generation
// 4. Recovery phase: Plaintext extraction
static void BM_SimplePIR(benchmark::State &state) {
  for (auto _ : state) {
    // Phase 0: Context initialization (excluded from timing)
    state.PauseTiming();
    auto ctx = SetupContext();  // Generate test parameters

    // Initialize server/client with test configuration
    std::unique_ptr<pir::simple::SimplePirServer> server;
    std::unique_ptr<pir::simple::SimplePirClient> client;
    server = std::make_unique<pir::simple::SimplePirServer>(
        ctx.dimension, ctx.q, ctx.N, ctx.p);
    client = std::make_unique<pir::simple::SimplePirClient>(
        ctx.dimension, ctx.q, ctx.N, ctx.p, ctx.radius, ctx.sigma);

    // Configure cryptographic materials
    server->SetDatabase(ctx.database);
    server->GenerateLweMatrix();
    auto server_seed = server->GetSeed();

    state.ResumeTiming();

    // Phase 1: Setup
    auto server_hint_vec = server->GetHint();
    uint128_t client_seed;
    std::vector<uint64_t> client_hint_vec;

    auto lctxs = yacl::link::test::SetupWorld(2);

    // Send LWE matrix for client
    auto sender =
        std::async([&] { pir::simple::SendUint128(server_seed, lctxs[0]); });
    auto receiver =
        std::async([&] { pir::simple::RecvUint128(client_seed, lctxs[1]); });
    sender.get();
    receiver.get();

    // Send hint to client
    sender =
        std::async([&] { pir::simple::SendVector(server_hint_vec, lctxs[0]); });
    receiver =
        std::async([&] { pir::simple::RecvVector(client_hint_vec, lctxs[1]); });
    sender.get();
    receiver.get();
    client->Setup(client_seed, client_hint_vec);

    // Phase 2: Query
    size_t idx = 10;
    auto client_query = client->Query(idx);
    std::vector<uint64_t> server_query_received;

    sender =
        std::async([&] { pir::simple::SendVector(client_query, lctxs[0]); });
    receiver = std::async(
        [&] { pir::simple::RecvVector(server_query_received, lctxs[1]); });
    sender.get();
    receiver.get();

    // Phase 3: Answer
    auto server_answer = server->Answer(server_query_received);
    std::vector<uint64_t> answer_received;

    sender =
        std::async([&] { pir::simple::SendVector(server_answer, lctxs[0]); });
    receiver =
        std::async([&] { pir::simple::RecvVector(answer_received, lctxs[1]); });
    sender.get();
    receiver.get();

    // Phase 4: Recover
    client->Recover(answer_received);  // Decrypt and validate retrieved value
  }
}

// Register benchmark with timing in milliseconds
BENCHMARK(BM_SimplePIR)->Unit(benchmark::kMillisecond);
}  // namespace
