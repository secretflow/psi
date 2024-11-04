#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

#include "experimental/pir/piano/client.h"
#include "experimental/pir/piano/serialize.h"
#include "experimental/pir/piano/server.h"
#include "experimental/pir/piano/util.h"

struct TestParams {
  uint64_t db_size;
  uint64_t db_seed;
  uint64_t thread_num;
  uint64_t query_num;
  bool is_total_query_num;
};

namespace pir::piano {

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

std::vector<DBEntry> RunClient(QueryServiceClient& client,
                               const std::vector<uint64_t>& queries) {
  client.FetchFullDB();
  return client.OnlineMultipleQueries(queries);
}

std::vector<DBEntry> getResults(const std::vector<uint64_t>& queries,
                                const TestParams& params) {
  std::vector<DBEntry> expected_results;
  expected_results.reserve(queries.size());

  for (const auto& x : queries) {
    expected_results.push_back(GenDBEntry(params.db_seed, x));
  }

  return expected_results;
}

class PianoTest : public testing::TestWithParam<TestParams> {};

TEST_P(PianoTest, Works) {
  auto params = GetParam();
  constexpr int kWorldSize = 2;
  const auto contexts = yacl::link::test::SetupWorld(kWorldSize);

  SPDLOG_INFO("DB N: %lu, Entry Size %lu Bytes, DB Size %lu MB\n",
              params.db_size, DBEntrySize,
              params.db_size * DBEntrySize / 1024 / 1024);

  auto [ChunkSize, SetSize] = GenParams(params.db_size);
  SPDLOG_INFO("Chunk Size: %lu, Set Size: %lu\n", ChunkSize, SetSize);

  std::vector<uint64_t> DB;
  DB.assign(ChunkSize * SetSize * DBEntryLength, 0);
  SPDLOG_INFO("DB Real N: %lu\n", DB.size());

  for (uint64_t i = 0; i < DB.size() / DBEntryLength; ++i) {
    auto entry = GenDBEntry(params.db_seed, i);
    std::memcpy(&DB[i * DBEntryLength], entry.data(),
                DBEntryLength * sizeof(uint64_t));
  }

  QueryServiceClient client(params.db_size, params.thread_num, contexts[1]);

  const auto actual_query_num =
      params.is_total_query_num ? client.totalQueryNum : params.query_num;
  auto queries = GenerateQueries(actual_query_num, DB.size());

  yacl::link::RecvTimeoutGuard guard(contexts[0], 1000000);
  QueryServiceServer server(DB, contexts[0], SetSize, ChunkSize);

  std::promise<void> exitSignal;
  std::future<void> futureObj = exitSignal.get_future();
  auto server_future =
      std::async(std::launch::async, &QueryServiceServer::Start,
                 std::ref(server), std::move(futureObj));
  auto client_future = std::async(std::launch::async, RunClient,
                                  std::ref(client), std::cref(queries));

  auto results = client_future.get();
  auto expected_results = getResults(queries, params);

  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i], expected_results[i])
        << "Mismatch at index " << queries[i];
  }

  exitSignal.set_value();
  server_future.get();
}

// [8m, 128m, 256m]
INSTANTIATE_TEST_SUITE_P(
    PianoTestInstances, PianoTest,
    ::testing::Values(TestParams{131072, 1211212, 8, 1000, false},
                      TestParams{2097152, 6405285, 8, 1000, false},
                      TestParams{4194304, 7539870, 16, 1000, false}));
}
