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

std::vector<uint64_t> GenerateQueries(const uint64_t query_num,
                                      const uint64_t entry_num) {
  std::vector<uint64_t> queries;
  queries.reserve(query_num);

  yacl::crypto::Prg<uint64_t> prg(yacl::crypto::SecureRandU64());
  for (uint64_t q = 0; q < query_num; ++q) {
    queries.push_back(prg() % entry_num);
  }

  return queries;
}

std::vector<uint8_t> FNVHash(uint64_t key) {
  constexpr uint64_t FNV_offset_basis = 14695981039346656037ULL;
  uint64_t hash = FNV_offset_basis;

  for (int i = 0; i < 8; ++i) {
    constexpr uint64_t FNV_prime = 1099511628211ULL;
    const auto byte = static_cast<uint8_t>(key & 0xFF);
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV_prime;
    key >>= 8;
  }

  std::vector<uint8_t> hash_bytes(8);
  for (size_t i = 0; i < 8; ++i) {
    hash_bytes[i] = static_cast<uint8_t>((hash >> (i * 8)) & 0xFF);
  }

  return hash_bytes;
}

std::vector<uint8_t> CreateDatabase(const uint64_t entry_size,
                                    const uint64_t entry_num,
                                    const uint64_t db_seed) {
  const auto [ChunkSize, SetSize] = pir::piano::GenParams(entry_num);
  std::vector<uint8_t> DB;
  DB.assign(ChunkSize * SetSize * entry_size, 0);

  for (uint64_t i = 0; i < DB.size() / entry_size; ++i) {
    auto entry =
        pir::piano::DBEntry::GenDBEntry(entry_size, db_seed, i, FNVHash);
    std::memcpy(&DB[i * entry_size], entry.data().data(),
                entry_size * sizeof(uint8_t));
  }

  return DB;
}

void SetupAndRunServer(
    const std::shared_ptr<yacl::link::Context>& server_context,
    const uint64_t entry_size, const uint64_t entry_num,
    std::promise<void>& exit_signal, std::vector<uint8_t>& db) {
  const auto [ChunkSize, SetSize] = pir::piano::GenParams(entry_num);
  pir::piano::QueryServiceServer server(db, server_context, SetSize, ChunkSize,
                                        entry_size);
  server.Start(exit_signal.get_future());
}

std::vector<pir::piano::DBEntry> SetupAndRunClient(
    const uint64_t entry_num, const uint64_t thread_num,
    const uint64_t entry_size,
    const std::shared_ptr<yacl::link::Context>& client_context,
    const std::vector<uint64_t>& queries) {
  pir::piano::QueryServiceClient client(entry_num, thread_num, entry_size,
                                        client_context);
  client.FetchFullDB();
  return client.OnlineMultipleQueries(queries);
}

}  // namespace

static void BM_PianoPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    const uint64_t entry_size = state.range(0);
    const uint64_t entry_num = state.range(1) / entry_size / CHAR_BIT;
    const uint64_t query_num = state.range(2);
    constexpr uint64_t db_seed = 2315127;
    uint64_t thread_num = 8;

    constexpr int kWorldSize = 2;
    const auto contexts = yacl::link::test::SetupWorld(kWorldSize);
    yacl::link::RecvTimeoutGuard guard(contexts[0], 1000000);

    auto db = CreateDatabase(entry_size, entry_num, db_seed);
    auto queries = GenerateQueries(query_num, entry_num);

    state.ResumeTiming();
    std::promise<void> exitSignal;
    auto server_future =
        std::async(std::launch::async, SetupAndRunServer, contexts[0],
                   entry_size, entry_num, std::ref(exitSignal), std::ref(db));

    auto client_future =
        std::async(std::launch::async, SetupAndRunClient, entry_num, thread_num,
                   entry_size, contexts[1], std::cref(queries));
    auto results = client_future.get();

    exitSignal.set_value();
    server_future.get();
  }
}

BENCHMARK(BM_PianoPir)
    ->Unit(benchmark::kMillisecond)
    ->Args({4, 1 << 20, 1000})
    ->Args({4, 2 << 20, 1000})
    ->Args({8, 1 << 20, 1000})
    ->Args({8, 2 << 20, 1000});
