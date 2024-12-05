// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/sealpir/seal_pir.h"

#include <seal/seal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using namespace std::chrono;
using namespace std;
using namespace seal;

namespace psi::sealpir {
namespace {
struct TestParams {
  uint32_t N = 4096;
  uint64_t num_of_items;
  uint64_t size_per_item = 288;
  uint64_t ind_degree = 0;
  uint32_t d = 2;
  uint32_t logt = 20;
  bool isSerialized = false;
};
}  // namespace
class SealPirTest : public testing::TestWithParam<TestParams> {};

TEST_P(SealPirTest, Works) {
  auto params = GetParam();
  uint32_t num_of_items = params.num_of_items;
  uint32_t size_per_item = params.size_per_item;
  uint32_t N = params.N;
  uint32_t d = params.d;
  bool isSerialized = params.isSerialized;
  SPDLOG_INFO(
      "N: {}, size_per_item: {} bytes, num_of_items: 2^{:.2f} = {}, "
      "ind_degree(Indistinguishable degree) {}, dimension: {}",
      N, size_per_item, std::log2(num_of_items), num_of_items,
      params.ind_degree, d);

  SealPirOptions options{params.N, params.num_of_items, params.size_per_item,
                         params.ind_degree, params.d};

  // Initialize PIR Server
  SPDLOG_INFO("Main: Initializing server and client");
  SealPirClient client(options);

  std::shared_ptr<IDbPlaintextStore> plaintext_store =
      std::make_shared<MemoryDbPlaintextStore>();
  SealPirServer server(options, plaintext_store);

  SPDLOG_INFO("Main: Initializing the database (this may take some time) ...");

  vector<uint8_t> db_data(num_of_items * size_per_item);
  random_device rd;
  for (uint64_t i = 0; i < num_of_items; i++) {
    for (uint64_t j = 0; j < size_per_item; j++) {
      uint8_t val = rd() % 256;
      db_data[(i * size_per_item) + j] = val;
    }
  }
  shared_ptr<IDbElementProvider> db_provider =
      make_shared<MemoryDbElementProvider>(std::move(db_data),
                                           params.size_per_item);

  // Measure database setup
  server.SetDatabaseByProvider(db_provider);
  SPDLOG_INFO("Main: database pre processed ");

  // Set galois key for client with id 0
  SPDLOG_INFO("Main: Setting Galois keys...");
  if (isSerialized) {
    string galois_keys_str = client.SerializeSealObject<seal::GaloisKeys>(
        client.GenerateGaloisKeys());
    server.SetGaloisKey(
        0, server.DeSerializeSealObject<seal::GaloisKeys>(galois_keys_str));
  } else {
    GaloisKeys galois_keys = client.GenerateGaloisKeys();
    server.SetGaloisKey(0, galois_keys);
  }

  uint64_t ele_index = rd() % num_of_items;  // element in DB at random position

  uint64_t query_index = ele_index;
  size_t start_pos = 0;
  if (params.ind_degree > 0) {
    query_index = ele_index % params.ind_degree;
    start_pos = ele_index - query_index;
  }

  uint64_t index = client.GetFVIndex(query_index);    // index of FV plaintext
  uint64_t offset = client.GetFVOffset(query_index);  // offset in FV plaintext
  SPDLOG_INFO("Main: element index = {} from [0, {}]", ele_index,
              num_of_items - 1);
  SPDLOG_INFO("Main: FV index = {}, FV offset = {}", index, offset);

  // Measure query generation

  vector<uint8_t> elems;
  if (isSerialized) {
    uint64_t offset;
    yacl::Buffer query_buffer = client.GenerateIndexQuery(ele_index, offset);
    SPDLOG_INFO("Main: query generated");

    yacl::Buffer reply_buffer = server.GenerateIndexReply(query_buffer);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeIndexReply(reply_buffer, offset);
    SPDLOG_INFO("Main: reply decoded");
  } else {
    SealPir::PirQuery query = client.GenerateQuery(index);
    SPDLOG_INFO("Main: query generated");

    SealPir::PirReply reply = server.GenerateReply(query, start_pos, 0);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeReply(reply, offset);
    SPDLOG_INFO("Main: reply decoded");
  }
  SPDLOG_INFO("Main: query finished");
  EXPECT_EQ(elems.size(), size_per_item);

  // Check that we retrieved the correct element
  EXPECT_EQ(elems, db_provider->ReadElement(ele_index * size_per_item));
  SPDLOG_INFO("Main: PIR result correct!");
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, SealPirTest,
    testing::Values(TestParams{4096, 1000}, TestParams{4096, 1000, 10},
                    TestParams{4096, 1000, 20}, TestParams{4096, 1000, 400},
                    TestParams{4096, 203, 288, 100},
                    TestParams{4096, 3000, 288, 1000},
                    // N = 8192
                    TestParams{8192, 1000}, TestParams{8192, 1000, 10},
                    TestParams{8192, 1000, 20}, TestParams{8192, 1000, 400},
                    TestParams{8192, 203, 288, 100},
                    TestParams{8192, 3000, 288, 1000},
                    // large num items
                    TestParams{4096, 1 << 16, 10, 0, 2},
                    TestParams{4096, 1 << 18, 10, 0, 2},
                    TestParams{4096, 1 << 20, 10, 0, 2},
                    // corner case
                    TestParams{4096, (1 << 22) - (1 << 10), 10},
                    // serializing
                    TestParams{4096, 1 << 18, 10, 0, 2, 20, true},
                    TestParams{4096, 1000, 288, 100, 2, 20, true},
                    TestParams{8192, 1000, 288, 0, 2, 20, true}));
}  // namespace psi::sealpir