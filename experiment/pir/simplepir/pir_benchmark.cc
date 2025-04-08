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
#include "client.h"
#include "server.h"
#include "yacl/link/test_util.h"

namespace {
struct TestContext {
  size_t dimension = 1 << 10;
  size_t N = 1ULL << 12;
  uint64_t q = 1ULL << 32;
  uint64_t p = 991;
  int radius = 4;
  double sigma = 6.8;
  std::vector<std::vector<uint64_t>> A;
};

// Initializes test context with parameters
// 1. Generates random LWE matrix A
// 2. Configures parameters for lattice-based PIR scheme
static TestContext SetupContext() {
  TestContext ctx;
  ctx.A.resize(ctx.dimension);
  size_t row = static_cast<size_t>(sqrt(ctx.N));
  for (size_t i = 0; i < ctx.dimension; i++) {
    ctx.A[i] = pir::simple::GenerateRandomVector(row, ctx.q);
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
    server->SetA_(ctx.A);
    server->GenerateDatabase();
    client->MatrixTranspose(ctx.A);

    state.ResumeTiming();

    // Phase 1: Setup
    auto lctxs = yacl::link::test::SetupWorld(2);
    auto server_setup = std::async([&] { server->ServerSetup(lctxs[0]); });
    auto client_setup = std::async([&] { client->ClientSetup(lctxs[1]); });
    server_setup.get();
    client_setup.get();

    // Phase 2: Query
    size_t idx = 10;
    auto client_query = std::async([&] { client->ClientQuery(idx, lctxs[0]); });
    auto server_query = std::async([&] { server->ServerQuery(lctxs[1]); });
    client_query.get();
    server_query.get();

    // Phase 3: Answer
    auto server_answer = std::async([&] { server->ServerAnswer(lctxs[0]); });
    auto client_answer = std::async([&] { client->ClientAnswer(lctxs[1]); });
    server_answer.get();
    client_answer.get();

    // Phase 4: Recover
    client->ClientRecover();  // Decrypt and validate retrieved value
  }
}

// Register benchmark with timing in milliseconds
BENCHMARK(BM_SimplePIR)->Unit(benchmark::kMillisecond);
}  // namespace
