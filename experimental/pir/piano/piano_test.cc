#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "experimental/pir/piano/client.h"
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

// Generate a set of uniformly distributed random query indices within the
// database entry range for testing purposes
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

// Simulate direct database lookup using plain-text indices to verify the
// correctness of PIR scheme
std::vector<DBEntry> GetPlainResults(const std::vector<uint64_t>& queries,
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
  const int world_size = 2;
  uint64_t entry_num = params.db_size / params.entry_size / CHAR_BIT;
  const auto contexts = yacl::link::test::SetupWorld(world_size);

  SPDLOG_INFO(
      "Database summary: total entries: {}, each entry size: {} bytes, total "
      "database size: {:.2f} MB",
      entry_num, params.entry_size,
      static_cast<double>(entry_num * params.entry_size) / (1024 * 1024));

  auto [chunk_size, set_size] = GenChunkParams(entry_num);
  SPDLOG_INFO("Generated parameters: chunk_size: {}, set_size: {}", chunk_size,
              set_size);

  SPDLOG_INFO("Generating database with seed: {}", params.db_seed);
  std::vector<uint8_t> database;
  database.assign(entry_num * params.entry_size, 0);

  for (uint64_t i = 0; i < entry_num; ++i) {
    auto entry =
        DBEntry::GenDBEntry(params.entry_size, params.db_seed, i, FNVHash);
    std::memcpy(&database[i * params.entry_size], entry.GetData().data(),
                params.entry_size * sizeof(uint8_t));
  }

  SPDLOG_INFO("Initializing query service: server and client");
  QueryServiceServer server(contexts[0], database, entry_num,
                            params.entry_size);
  QueryServiceClient client(contexts[1], entry_num, params.thread_num,
                            params.entry_size);

  const auto actual_query_num = params.is_total_query_num
                                    ? client.GetTotalQueryNumber()
                                    : params.query_num;
  SPDLOG_INFO("Generating {} test queries", actual_query_num);
  const auto queries = GenTestQueries(actual_query_num, entry_num);

  SPDLOG_INFO("Starting preprocess phase");
  auto client_preprocess_future =
      std::async(std::launch::async, [&client]() { client.FetchFullDB(); });

  auto server_preprocess_future = std::async(
      std::launch::async, [&server]() { server.HandleFetchFullDB(); });

  client_preprocess_future.get();
  server_preprocess_future.get();

  SPDLOG_INFO("Starting online query phase");
  std::promise<void> stop_signal;
  std::future<void> stop_future = stop_signal.get_future();

  auto client_query_future = std::async(
      std::launch::async,
      [&client, &queries]() { return client.OnlineMultipleQueries(queries); });

  auto server_query_future = std::async(
      std::launch::async,
      [&server, &stop_future]() { server.HandleMultipleQueries(stop_future); });

  const auto pir_results = client_query_future.get();
  const auto expected_results = GetPlainResults(queries, params);
  stop_signal.set_value();
  server_query_future.get();

  SPDLOG_INFO("Verifying {} query results", pir_results.size());
  for (size_t i = 0; i < pir_results.size(); ++i) {
    EXPECT_EQ(pir_results[i].GetData(), expected_results[i].GetData())
        << "Mismatch at index " << queries[i];
  }
}

// [8m, 128m, 256m]  units are in bits
INSTANTIATE_TEST_SUITE_P(
    PianoTestInstances, PianoTest,
    ::testing::Values(TestParams{8, 8 << 20, 1211212, 8, 1000, false},
                      TestParams{8, 128 << 20, 6405285, 8, 1000, false},
                      TestParams{8, 256 << 20, 7539870, 16, 1000, false}));
}  // namespace pir::piano
