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

#include <string>
#include <thread>
#include <vector>

#include "benchmark/benchmark.h"
#include "pir_client.h"
#include "pir_server.h"

namespace {
struct TestContext {
  size_t n = 1 << 10;
  size_t N = 1ULL << 12;
  size_t q = 1ULL << 32;
  size_t p = 991;
  std::string ip = "127.0.0.1";
  int port = 12345;
  int radius = 4;
  double sigma = 6.8;
  std::vector<std::vector<__uint128_t>> A;
};

static TestContext SetupContext() {
  TestContext ctx;
  ctx.A.resize(ctx.n);
  size_t row = static_cast<size_t>(sqrt(ctx.N));
  for (size_t i = 0; i < ctx.n; i++) {
    ctx.A[i] = pir::simple::generate_random_vector(row, ctx.q);
  }
  return ctx;
}

static void BM_SimplePIR(benchmark::State &state) {
  auto ctx = SetupContext();

  pir::simple::PIRServer server(ctx.n, ctx.q, ctx.N, ctx.p, ctx.ip, ctx.port);
  pir::simple::PIRClient client(ctx.n, ctx.q, ctx.N, ctx.p, ctx.radius,
                                ctx.sigma, ctx.ip, ctx.port);

  server.set_A_(ctx.A);
  server.generate_database();
  client.matrix_transpose_128(ctx.A);

  for (auto _ : state) {
    auto server_setup = std::thread([&server]() { server.server_setup(); });
    auto client_setup = std::thread([&client]() { client.client_setup(); });
    server_setup.join();
    client_setup.join();

    size_t idx = 10;

    auto server_query = std::thread([&server]() { server.server_query(); });
    auto client_query =
        std::thread([&client, idx]() { client.client_query(idx); });
    server_query.join();
    client_query.join();

    auto server_answer = std::thread([&server]() { server.server_answer(); });
    auto client_answer = std::thread([&client]() { client.client_answer(); });
    server_answer.join();
    client_answer.join();

    client.client_recover();
  }
}
BENCHMARK(BM_SimplePIR)->Unit(benchmark::kMillisecond);
}  // namespace

// BENCHMARK_MAIN();
