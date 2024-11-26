#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "experimental/pir/piano/client.h"
#include "experimental/pir/piano/serialize.h"
#include "experimental/pir/piano/server.h"
#include "experimental/pir/piano/util.h"
#include "gtest/gtest.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

struct TestParams {
  uint64_t entry_size;
  uint64_t db_size;
  uint64_t db_seed;
  uint64_t thread_num;
  uint64_t query_num;
  bool is_total_query_num;
};

namespace pir::piano {

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

std::vector<DBEntry> RunClient(QueryServiceClient& client,
                               const std::vector<uint64_t>& queries) {
  client.FetchFullDB();
  return client.OnlineMultipleQueries(queries);
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

std::vector<DBEntry> getResults(const std::vector<uint64_t>& queries,
                                const TestParams& params) {
  std::vector<DBEntry> expected_results;
  expected_results.reserve(queries.size());

  for (const auto& x : queries) {
    expected_results.push_back(
        DBEntry::GenDBEntry(params.entry_size, params.db_seed, x, FNVHash));
  }

  return expected_results;
}

class PianoTest : public testing::TestWithParam<TestParams> {};

TEST_P(PianoTest, Works) {
  auto params = GetParam();
  constexpr int kWorldSize = 2;
  uint64_t entry_num = params.db_size / params.entry_size / CHAR_BIT;
  const auto contexts = yacl::link::test::SetupWorld(kWorldSize);

  SPDLOG_INFO("DB N: %lu, Entry Size %lu Bytes, DB Size %lu MB\n", entry_num,
              params.entry_size, entry_num * params.entry_size / 1024 / 1024);

  auto [ChunkSize, SetSize] = GenParams(entry_num);
  SPDLOG_INFO("Chunk Size: %lu, Set Size: %lu\n", ChunkSize, SetSize);

  std::vector<uint8_t> DB;
  DB.assign(ChunkSize * SetSize * params.entry_size, 0);
  SPDLOG_INFO("DB Real N: %lu\n", DB.size());

  for (uint64_t i = 0; i < DB.size() / params.entry_size; ++i) {
    auto entry =
        DBEntry::GenDBEntry(params.entry_size, params.db_seed, i, FNVHash);
    std::memcpy(&DB[i * params.entry_size], entry.data().data(),
                params.entry_size * sizeof(uint8_t));
  }

  QueryServiceClient client(entry_num, params.thread_num, params.entry_size,
                            contexts[1]);

  const auto actual_query_num = params.is_total_query_num
                                    ? client.getTotalQueryNumber()
                                    : params.query_num;
  const auto queries = GenerateQueries(actual_query_num, entry_num);

  yacl::link::RecvTimeoutGuard guard(contexts[0], 1000000);
  QueryServiceServer server(DB, contexts[0], SetSize, ChunkSize,
                            params.entry_size);

  std::promise<void> exitSignal;
  std::future<void> futureObj = exitSignal.get_future();
  auto server_future =
      std::async(std::launch::async, &QueryServiceServer::Start,
                 std::ref(server), std::move(futureObj));
  auto client_future = std::async(std::launch::async, RunClient,
                                  std::ref(client), std::cref(queries));

  const auto results = client_future.get();
  const auto expected_results = getResults(queries, params);

  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i].data(), expected_results[i].data())
        << "Mismatch at index " << queries[i];
  }

  exitSignal.set_value();
  server_future.get();
}

// [8m, 128m, 256m]  units are in bits
INSTANTIATE_TEST_SUITE_P(
    PianoTestInstances, PianoTest,
    ::testing::Values(TestParams{8, 8 << 20, 1211212, 8, 1000, false},
                      TestParams{8, 128 << 20, 6405285, 8, 1000, false},
                      TestParams{8, 256 << 20, 7539870, 16, 1000, false}));
}  // namespace pir::piano
