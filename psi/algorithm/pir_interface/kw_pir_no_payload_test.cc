// Copyright 2024 Ant Group Co., Ltd.
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

#include <random>
#include <utility>

#include "gtest/gtest.h"
#include "kw_pir.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/hash/hash_utils.h"

#include "psi/algorithm/pir_interface/kw_pir.h"
#include "psi/algorithm/sealpir/seal_pir.h"

namespace psi::pir {

struct TestParams {
  // we only need to set the first two fields
  uint64_t num_input = 1000;
  uint64_t value_size = 0;
  uint64_t key_size = 16;

  uint64_t num_hash = 3;
  double scale_factor = 1.3;
  uint64_t max_try_count = 128;

  uint32_t N = 4096;
  uint32_t d = 2;
  uint64_t ind_degree = 0;

  uint64_t NumBins() const {
    uint64_t num_bins = num_input * scale_factor;
    // for stashless cuckooHash
    // when num_input < 256, num_bins add  8 for safe
    // reduce the probability of using stash
    if (num_input < 256) {
      num_bins += 8;
    }
    return num_bins;
  }
};
class KwpirTest : public testing::TestWithParam<TestParams> {};
TEST_P(KwpirTest, Works) {
  TestParams params = GetParam();
  uint64_t num_input = params.num_input;
  uint64_t num_hash = params.num_hash;
  uint64_t max_try_count = params.max_try_count;
  double scale_factor = params.scale_factor;
  uint64_t key_size = params.key_size;
  uint64_t value_size = params.value_size;

  YACL_ENFORCE_EQ(value_size, 0U);

  std::set<std::vector<uint8_t>> db_vec_store;

  while (db_vec_store.size() < num_input) {
    db_vec_store.insert(yacl::crypto::RandBytes(key_size));
  }

  std::vector<yacl::ByteContainerView> db_vec;
  db_vec.reserve(num_input);
  // for (uint64_t i = 0; i < num_input; ++i) {
  //   db_vec.emplace_back(db_vec_store[i]);
  // }
  for (const auto& d : db_vec_store) {
    db_vec.emplace_back(d);
  }

  psi::sealpir::SealPirOptions seal_options{params.N, params.NumBins(),
                                            key_size + value_size, params.d};

  std::unique_ptr<psi::sealpir::SealPirServer> seal_server(
      new psi::sealpir::SealPirServer(seal_options));
  std::unique_ptr<psi::sealpir::SealPirClient> seal_client(
      new psi::sealpir::SealPirClient(seal_options));

  auto galois_keys = seal_client->GeneratePksBuffer();

  CuckooIndex::Options cuckoo_options{num_input, 0, num_hash, scale_factor,
                                      max_try_count};
  KwPirOptions options{cuckoo_options, value_size};
  KwPirServer tmp_server(options, std::move(seal_server));
  KwPirClient client(options, std::move(seal_client));

  SPDLOG_INFO("setting database...\n");
  tmp_server.GenerateFromRawKeyData(db_vec);

  std::string file_name = "tmp_server.bin";
  std::ofstream output(file_name, std::ios::binary);
  tmp_server.Dump(output);
  output.close();
  SPDLOG_INFO("finished server Dump");

  int fd = open(file_name.c_str(), O_RDONLY);
  google::protobuf::io::FileInputStream input(fd);
  auto server = KwPirServer::Load(input);
  input.Close();
  SPDLOG_INFO("finished server Load");

  SPDLOG_INFO("A test case, where the query key in the DB");

  uint64_t ele_index = yacl::crypto::RandU64() % num_input;
  yacl::ByteContainerView query_key = db_vec[ele_index];

  SPDLOG_INFO("generating query...\n");
  auto [query_vec, offset_vec] = client.GenerateQuery(query_key);
  SPDLOG_INFO("generating reply...\n");
  std::vector<yacl::Buffer> reply_vec;
  for (auto& query : query_vec) {
    reply_vec.push_back(server->Response(query, galois_keys));
  }
  SPDLOG_INFO("decoding reply...\n");
  std::vector<std::vector<uint8_t>> decoded_vec =
      client.DecodeResponse(reply_vec, offset_vec);
  EXPECT_EQ(decoded_vec.size(), num_hash);
  EXPECT_EQ(decoded_vec[0].size(), key_size);

  bool success = false;
  auto query_hash = yacl::crypto::Blake3_128(query_key);
  for (uint32_t i = 0; i < num_hash; ++i) {
    KwPir::HashType key;
    memcpy(&key, decoded_vec[i].data(), sizeof(KwPir::HashType));

    if (key == query_hash) {
      success = true;
      break;
    }
  }
  EXPECT_TRUE(success);

  SPDLOG_INFO("A test case, where the query key not in the DB");
  // ensure query_key not in db
  auto query_key_bytes = yacl::crypto::RandBytes(key_size);
  auto it = db_vec_store.find(query_key_bytes);

  while (it != db_vec_store.end()) {
    query_key_bytes = yacl::crypto::RandBytes(key_size);
    it = db_vec_store.find(query_key_bytes);
  }
  query_key = yacl::ByteContainerView{query_key_bytes};

  SPDLOG_INFO("generating query...\n");
  std::tie(query_vec, offset_vec) = client.GenerateQuery(query_key);

  SPDLOG_INFO("generating reply...\n");
  reply_vec.resize(0);
  for (auto& query : query_vec) {
    reply_vec.push_back(server->Response(query, galois_keys));
  }

  SPDLOG_INFO("decoding reply...\n");
  decoded_vec = client.DecodeResponse(reply_vec, offset_vec);
  EXPECT_EQ(decoded_vec.size(), num_hash);

  success = false;
  query_hash = yacl::crypto::Blake3_128(query_key);
  for (uint32_t i = 0; i < num_hash; ++i) {
    KwPir::HashType key;
    std::memcpy(&key, decoded_vec[i].data(), sizeof(KwPir::HashType));
    if (key == query_hash) {
      success = true;
      break;
    }
  }
  EXPECT_FALSE(success);
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, KwpirTest,
                         testing::Values(TestParams{100000}, TestParams{10000},
                                         TestParams{1000}));

}  // namespace psi::pir
