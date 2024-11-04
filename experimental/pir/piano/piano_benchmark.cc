#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

#include "experimental/pir/piano/client.h"
#include "experimental/pir/piano/server.h"
#include "experimental/pir/piano/util.h"

namespace {

std::vector<uint64_t> GenerateQueries(const uint64_t query_num,
                                      const uint64_t db_size) {
  std::vector<uint64_t> queries;
  queries.reserve(query_num);

  std::mt19937_64 rng(yacl::crypto::FastRandU64());
  for (uint64_t q = 0; q < query_num; ++q) {
    queries.push_back(rng() % db_size);
  }

  return queries;
}

std::vector<uint64_t> CreateDatabase(const uint64_t db_size,
                                     const uint64_t db_seed) {
  const auto [ChunkSize, SetSize] = pir::piano::GenParams(db_size);
  std::vector<uint64_t> DB;
  DB.assign(ChunkSize * SetSize * pir::piano::DBEntryLength, 0);

  for (uint64_t i = 0; i < DB.size() / pir::piano::DBEntryLength; ++i) {
    auto entry = pir::piano::GenDBEntry(db_seed, i);
    std::memcpy(&DB[i * pir::piano::DBEntryLength], entry.data(),
                pir::piano::DBEntryLength * sizeof(uint64_t));
  }

  return DB;
}

void SetupAndRunServer(
    const std::shared_ptr<yacl::link::Context>& server_context,
    const uint64_t db_size, std::promise<void>& exit_signal,
    std::vector<uint64_t>& db) {
  const auto [ChunkSize, SetSize] = pir::piano::GenParams(db_size);
  pir::piano::QueryServiceServer server(db, server_context, SetSize, ChunkSize);
  server.Start(exit_signal.get_future());
}

std::vector<pir::piano::DBEntry> SetupAndRunClient(
    const uint64_t db_size, const uint64_t thread_num,
    const std::shared_ptr<yacl::link::Context>& client_context,
    const std::vector<uint64_t>& queries) {
  pir::piano::QueryServiceClient client(db_size, thread_num, client_context);
  client.FetchFullDB();
  return client.OnlineMultipleQueries(queries);
}

}  // namespace

static void BM_PianoPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    uint64_t db_size = state.range(0) / sizeof(pir::piano::DBEntry);
    const uint64_t query_num = state.range(1);
    constexpr uint64_t db_seed = 2315127;
    uint64_t thread_num = 8;

    constexpr int kWorldSize = 2;
    const auto contexts = yacl::link::test::SetupWorld(kWorldSize);
    yacl::link::RecvTimeoutGuard guard(contexts[0], 1000000);

    auto db = CreateDatabase(db_size, db_seed);
    auto queries = GenerateQueries(query_num, db_size);

    state.ResumeTiming();
    std::promise<void> exitSignal;
    auto server_future =
        std::async(std::launch::async, SetupAndRunServer, contexts[0], db_size,
                   std::ref(exitSignal), std::ref(db));

    auto client_future =
        std::async(std::launch::async, SetupAndRunClient, db_size, thread_num,
                   contexts[1], std::cref(queries));
    auto results = client_future.get();

    exitSignal.set_value();
    server_future.get();
  }
}

// [1m, 16m, 64m, 128m]
BENCHMARK(BM_PianoPir)
    ->Unit(benchmark::kMillisecond)
    ->Args({1 << 20, 1000})
    ->Args({16 << 20, 1000})
    ->Args({64 << 20, 1000})
    ->Args({128 << 20, 1000});
