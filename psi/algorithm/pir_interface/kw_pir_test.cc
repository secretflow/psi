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
#include "spdlog/spdlog.h"
#include "yacl/crypto/hash/hash_utils.h"

#include "psi/algorithm/sealpir/seal_pir.h"

namespace psi::pir {

struct TestParams {
  uint64_t num_input = 1000;
  uint64_t num_hash = 3;
  double scale_factor = 1.3;
  uint64_t key_size = 16;
  uint64_t value_size = 256;
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

  psi::sealpir::SealPirOptions seal_options{params.N, params.NumBins(),
                                            key_size + value_size, params.d};

  std::unique_ptr<psi::sealpir::SealPirServer> seal_server(
      new psi::sealpir::SealPirServer(seal_options));
  std::unique_ptr<psi::sealpir::SealPirClient> seal_client(
      new psi::sealpir::SealPirClient(seal_options));

  seal::GaloisKeys galois_keys = seal_client->GenerateGaloisKeys();
  seal_server->SetGaloisKey(0, galois_keys);

  CuckooIndex::Options cuckoo_options{num_input, 0, num_hash, scale_factor,
                                      max_try_count};
  KwPirOptions options{cuckoo_options, key_size, value_size};
  KwPirServer server(options, std::move(seal_server));
  KwPirClient client(options, std::move(seal_client));

  SPDLOG_INFO("setting database...\n");
  server.SetDatabase(db_vec);

  uint64_t ele_index = yacl::crypto::RandU64() % num_input;
  yacl::ByteContainerView query_key = db_vec[ele_index].first;
  yacl::ByteContainerView query_value = db_vec[ele_index].second;

  // std::vector<uint64_t> offset_vec;
  SPDLOG_INFO("generating query...\n");
  auto [query_vec, offset_vec] = client.GenerateQuery(query_key);
  SPDLOG_INFO("generating reply...\n");
  std::vector<yacl::Buffer> reply_vec = server.GenerateResponse(query_vec);
  SPDLOG_INFO("decoding reply...\n");
  std::vector<std::vector<uint8_t>> decoded_vec =
      client.DecodeResponse(reply_vec, offset_vec);

  EXPECT_EQ(decoded_vec.size(), num_hash);
  bool success = false;
  for (uint32_t i = 0; i < num_hash; ++i) {
    std::vector<uint8_t> key(key_size);
    std::vector<uint8_t> value(value_size);
    memcpy(key.data(), decoded_vec[i].data(), key_size);
    memcpy(value.data(), decoded_vec[i].data() + key_size, value_size);

    if (key == query_key) {
      success = true;
      EXPECT_EQ(value, query_value);
      break;
    }
  }

  EXPECT_TRUE(success);
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, KwpirTest,
    testing::Values(TestParams{1000, 3, 1.3, 16, 256, 128, 4096, 2, 0},
                    TestParams{1000, 2, 2.4, 16, 256, 128, 4096, 2, 0},
                    TestParams{1000, 2, 2.4, 16, 20000, 128, 4096, 2, 0},
                    TestParams{1000, 3, 1.3, 16, 128, 128, 4096, 1, 400},
                    TestParams{1000, 2, 2.4, 16, 128, 128, 4096, 1, 400},
                    TestParams{203, 3, 1.3, 16, 8, 128, 4096, 1, 0},
                    TestParams{4000, 2, 2.4, 16, 256, 128, 4096, 2, 0},

                    // N = 8192
                    TestParams{1000, 3, 1.3, 16, 256, 128, 8192, 1, 0},
                    TestParams{1000, 2, 2.4, 16, 256, 128, 8192, 1, 0},
                    // TestParams{1000, 2, 2.4, 16, 20000, 128, 8192, 1, 0},
                    TestParams{3000, 3, 1.3, 16, 256, 128, 8192, 2, 1000},
                    TestParams{1000, 3, 1.3, 16, 16, 128, 8192, 2, 400},

                    // // large num items
                    TestParams{1 << 16, 3, 1.3, 16, 8, 128, 4096, 2, 0},
                    TestParams{1 << 16, 2, 2.4, 16, 8, 128, 4096, 2, 0},
                    TestParams{1 << 20, 3, 1.3, 16, 64, 128, 4096, 2, 0},
                    TestParams{(1 << 22) - (1 << 10), 3, 1.3, 16, 64, 128, 4096,
                               2, 0}));

}  // namespace psi::pir
