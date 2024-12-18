#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "experimental/pir/piano/client.h"
#include "experimental/pir/piano/server.h"
#include "experimental/pir/piano/util.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

namespace {

std::vector<uint64_t> GenTestQueries(const uint64_t query_num,
                                     const uint64_t entry_num) {
  std::vector<uint64_t> queries;
  queries.reserve(query_num);
  yacl::crypto::Prg<uint64_t> prg(yacl::crypto::SecureRandU64());
  for (uint64_t q = 0; q < query_num; ++q) {
    queries.push_back(prg() % entry_num);
  }
  return queries;
}

std::vector<uint8_t> CreateDatabase(const uint64_t entry_size,
                                    const uint64_t entry_num,
                                    const uint64_t db_seed) {
  std::vector<uint8_t> database;
  database.assign(entry_num * entry_size, 0);
  for (uint64_t i = 0; i < entry_num; ++i) {
    auto entry = pir::piano::DBEntry::GenDBEntry(entry_size, db_seed, i,
                                                 pir::piano::FNVHash);
    std::memcpy(&database[i * entry_size], entry.GetData().data(),
                entry_size * sizeof(uint8_t));
  }
  return database;
}

}  // namespace

static void BM_PianoPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t entry_size = state.range(0);
    const uint64_t entry_num = state.range(1) / entry_size / CHAR_BIT;
    const uint64_t query_num = state.range(2);
    const uint64_t db_seed = yacl::crypto::FastRandU64();
    const uint64_t thread_num = 8;

    const int world_size = 2;
    const auto contexts = yacl::link::test::SetupWorld(world_size);

    auto database = CreateDatabase(entry_size, entry_num, db_seed);
    auto queries = GenTestQueries(query_num, entry_num);

    state.ResumeTiming();
    pir::piano::QueryServiceServer server(contexts[0], database, entry_num,
                                          entry_size);
    pir::piano::QueryServiceClient client(contexts[1], entry_num, thread_num,
                                          entry_size);

    auto client_preprocess_future =
        std::async(std::launch::async, [&client]() { client.FetchFullDB(); });

    auto server_preprocess_future = std::async(
        std::launch::async, [&server]() { server.HandleFetchFullDB(); });

    client_preprocess_future.get();
    server_preprocess_future.get();

    std::promise<void> stop_signal;
    std::future<void> stop_future = stop_signal.get_future();

    auto client_query_future =
        std::async(std::launch::async, [&client, &queries]() {
          return client.OnlineMultipleQueries(queries);
        });

    auto server_query_future =
        std::async(std::launch::async, [&server, &stop_future]() {
          server.HandleMultipleQueries(stop_future);
        });

    const auto pir_results = client_query_future.get();
    stop_signal.set_value();
    server_query_future.get();
  }
}

BENCHMARK(BM_PianoPir)
    ->Unit(benchmark::kMillisecond)
    ->Args({4, 32 << 20, 1000})
    ->Args({4, 64 << 20, 1000})
    ->Args({4, 128 << 20, 1000})
    ->Args({8, 64 << 20, 1000})
    ->Args({8, 128 << 20, 1000})
    ->Args({8, 256 << 20, 1000});
