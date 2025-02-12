// Copyright 2024 The secretflow authors.
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

#include <spdlog/spdlog.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "experiment/pir/piano/client.h"
#include "experiment/pir/piano/server.h"
#include "experiment/pir/piano/util.h"
#include "gtest/gtest.h"

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
std::vector<uint64_t> GenerateTestQueries(uint64_t query_num,
                                          uint64_t entry_num) {
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
  uint64_t entry_num = params.db_size / params.entry_size / CHAR_BIT;

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
  QueryServiceServer server(database, entry_num, params.entry_size);
  QueryServiceClient client(entry_num, params.thread_num, params.entry_size);

  auto actual_query_num = params.is_total_query_num
                              ? client.GetTotalQueryNumber()
                              : params.query_num;
  SPDLOG_INFO("Generating {} test queries", actual_query_num);
  const auto queries = GenerateTestQueries(actual_query_num, entry_num);

  SPDLOG_INFO("Starting preprocess phase");
  auto chunk_number = client.GetChunkNumber();
  for (uint64_t chunk_index = 0; chunk_index < chunk_number; ++chunk_index) {
    yacl::Buffer chunk_buffer = server.GetDBChunk(chunk_index);
    client.PreprocessDBChunk(chunk_buffer);
  }

  SPDLOG_INFO("Starting online query phase");
  std::vector<DBEntry> pir_results;
  for (auto query_index : queries) {
    yacl::Buffer query_buffer = client.GenerateIndexQuery(query_index);
    yacl::Buffer reply_buffer = server.GenerateIndexReply(query_buffer);
    DBEntry result = client.RecoverIndexReply(reply_buffer);
    pir_results.push_back(result);
  }

  const auto expected_results = GetPlainResults(queries, params);
  SPDLOG_INFO("Verifying {} query results", pir_results.size());
  EXPECT_EQ(pir_results.size(), expected_results.size());
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
