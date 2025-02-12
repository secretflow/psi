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

#include <cstdint>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "experiment/pir/piano/client.h"
#include "experiment/pir/piano/server.h"
#include "experiment/pir/piano/util.h"

namespace {

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

std::vector<uint8_t> CreateDatabase(uint64_t entry_size, uint64_t entry_num,
                                    uint64_t db_seed) {
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
    uint64_t entry_size = state.range(0);
    uint64_t entry_num = state.range(1) / entry_size / CHAR_BIT;
    uint64_t query_num = state.range(2);
    uint64_t db_seed = yacl::crypto::FastRandU64();
    uint64_t thread_num = 8;

    auto database = CreateDatabase(entry_size, entry_num, db_seed);
    auto queries = GenerateTestQueries(query_num, entry_num);

    state.ResumeTiming();
    pir::piano::QueryServiceServer server(database, entry_num, entry_size);
    pir::piano::QueryServiceClient client(entry_num, thread_num, entry_size);

    auto chunk_number = client.GetChunkNumber();
    for (uint64_t chunk_index = 0; chunk_index < chunk_number; ++chunk_index) {
      yacl::Buffer chunk_buffer = server.GetDBChunk(chunk_index);
      client.PreprocessDBChunk(chunk_buffer);
    }

    std::vector<pir::piano::DBEntry> pir_results;
    for (auto query_index : queries) {
      yacl::Buffer query_buffer = client.GenerateIndexQuery(query_index);
      yacl::Buffer reply_buffer = server.GenerateIndexReply(query_buffer);
      pir::piano::DBEntry result = client.RecoverIndexReply(reply_buffer);
      pir_results.push_back(result);
    }
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
