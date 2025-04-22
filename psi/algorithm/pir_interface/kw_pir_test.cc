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

#include "psi/algorithm/pir_interface/kw_pir.h"

#include <random>
#include <utility>

#include "gtest/gtest.h"
#include "kw_pir.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/hash/hash_utils.h"

#include "psi/algorithm/sealpir/seal_pir.h"

#include "psi/algorithm/pir_interface/pir_type.pb.h"

namespace psi::pir {

struct TestParams {
  // we only need to set the first two fields
  PirType pir_type;
  uint64_t num_input = 1000;
  uint64_t value_size = 256;
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
  PirType pir_type = params.pir_type;
  uint64_t num_input = params.num_input;
  uint64_t num_hash = params.num_hash;
  uint64_t key_size = params.key_size;
  uint64_t value_size = params.value_size;

  SPDLOG_INFO("raw input num: {}, raw value size: {}", num_input, value_size);

  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
      db_vec_store;

  for (uint64_t i = 0; i < num_input; ++i) {
    std::vector<uint8_t> key = yacl::crypto::RandBytes(key_size);
    std::vector<uint8_t> value = yacl::crypto::RandBytes(value_size);
    db_vec_store.push_back(std::make_pair(key, value));
  }

  std::vector<std::pair<yacl::ByteContainerView, yacl::ByteContainerView>>
      db_vec;
  for (uint64_t i = 0; i < num_input; ++i) {
    db_vec.push_back(
        std::make_pair<yacl::ByteContainerView, yacl::ByteContainerView>(
            db_vec_store[i].first, db_vec_store[i].second));
  }

  auto tmp_server = CreateKwPirServer(pir_type, value_size, num_input);
  auto client = CreateKwPirClient(pir_type, value_size, num_input);
  auto pks = client->GeneratePksString();

  SPDLOG_INFO("setting database...\n");
  // raw key-value db ---> cuckoohash ----> raw index db ---> pir needed db,
  // specific format
  tmp_server->GenerateFromRawKeyValueData(db_vec);

  std::string file_name = "tmp_server.bin";
  std::ofstream output(file_name, std::ios::binary);
  tmp_server->Dump(output);
  output.close();
  SPDLOG_INFO("finished server Dump");

  int fd = open(file_name.c_str(), O_RDONLY);
  google::protobuf::io::FileInputStream input(fd);
  auto server = KwPirServer::Load(input);
  input.Close();
  SPDLOG_INFO("finished server Load");

  uint64_t ele_index = yacl::crypto::RandU64() % num_input;
  yacl::ByteContainerView query_key = db_vec[ele_index].first;
  auto hash_query_key = yacl::crypto::Blake3_128(query_key);
  yacl::ByteContainerView query_value = db_vec[ele_index].second;

  SPDLOG_INFO("generating query...\n");
  auto [query_vec, offset_vec] = client->GenerateQueryStr(query_key);
  SPDLOG_INFO("generating reply...\n");
  auto reply_vec = server->Response(query_vec, pks);
  SPDLOG_INFO("decoding reply...\n");
  std::vector<std::vector<uint8_t>> decoded_vec =
      client->DecodeResponse(reply_vec, offset_vec);

  EXPECT_EQ(decoded_vec.size(), num_hash);
  bool success = false;
  for (uint32_t i = 0; i < num_hash; ++i) {
    psi::pir::KwPir::HashType hash;
    std::vector<uint8_t> value(value_size);
    memcpy(&hash, decoded_vec[i].data(), sizeof(psi::pir::KwPir::HashType));
    memcpy(value.data(),
           decoded_vec[i].data() + sizeof(psi::pir::KwPir::HashType),
           value_size);

    if (hash == hash_query_key) {
      success = true;
      EXPECT_EQ(value, query_value);
      break;
    }
  }

  EXPECT_TRUE(success);
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, KwpirTest,
    testing::Values(TestParams{PirType::SPIRAL_PIR, 10000, 256, 16},
                    TestParams{PirType::SEAL_PIR, 10, 32, 16},
                    TestParams{PirType::SPIRAL_PIR, 100, 8192 * 10, 16},
                    TestParams{PirType::SEAL_PIR, 10000, 256, 16},
                    TestParams{PirType::SEAL_PIR, 100, 8192 * 10, 16}));

#if 0

TEST_P(KwpirTest, Works2) {
  TestParams params = GetParam();
  PirType pir_type = params.pir_type;
  uint64_t num_input = params.num_input;
  uint64_t key_size = params.key_size;
  uint64_t value_size = params.value_size;

  std::vector<std::string> keys;
  std::vector<std::string> values;
  for (uint64_t i = 0; i < num_input; ++i) {
    std::vector<uint8_t> key = yacl::crypto::RandBytes(key_size);
    keys.emplace_back(reinterpret_cast<char*>(key.data()), key.size());
    std::vector<uint8_t> value = yacl::crypto::RandBytes(value_size);
    values.emplace_back(reinterpret_cast<char*>(value.data()), value.size());
  }

  auto tmp_server = CreateKwPirServer(pir_type, value_size, num_input);
  auto client = CreateKwPirClient(pir_type, value_size, num_input);
  auto pks = client->GeneratePksString();

  SPDLOG_INFO("setting database...\n");
  tmp_server->GenerateFromRawKeyValueData(keys, values);

  std::string file_name = "tmp_server2.bin";
  std::ofstream output(file_name, std::ios::binary);
  tmp_server->Dump(output);
  output.close();
  SPDLOG_INFO("finished server Dump");

  int fd = open(file_name.c_str(), O_RDONLY);
  google::protobuf::io::FileInputStream input(fd);
  auto server = KwPirServer::Load(input);
  input.Close();
  SPDLOG_INFO("finished server Load");

  uint64_t ele_index = yacl::crypto::RandU64() % num_input;
  auto query_key = keys[ele_index];
  auto query_value = values[ele_index];

  SPDLOG_INFO("generating query...\n");
  auto ctx = client->GenerateQueryCtx(query_key);
  SPDLOG_INFO("generating reply...\n");
  auto reply_vec = server->Response(ctx.query_vec, pks);
  SPDLOG_INFO("decoding reply...\n");
  auto matched = client->DecodeResponse(&ctx, reply_vec);
  EXPECT_TRUE(matched);
  EXPECT_EQ(ctx.value, query_value);
}

INSTANTIATE_TEST_SUITE_P(
    Works2_Instances, KwpirTest,
    testing::Values(TestParams{PirType::SPIRAL_PIR, 10000, 256, 16},
                    TestParams{PirType::SPIRAL_PIR, 1000, 8192 * 10, 16},
                    TestParams{PirType::SEAL_PIR, 10000, 256, 16},
                    TestParams{PirType::SEAL_PIR, 1000, 8192 * 10, 16}));

#endif
}  // namespace psi::pir
