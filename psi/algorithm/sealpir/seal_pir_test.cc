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

#include "psi/algorithm/sealpir/seal_pir.h"

#include <seal/seal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

using namespace std::chrono;
using namespace std;
using namespace seal;

namespace psi::sealpir {
namespace {
struct TestParams {
  uint32_t N = 4096;
  uint64_t rows;
  uint64_t row_byte_len = 256;
  bool isSerialized = false;
};
}  // namespace
class SealPirTest : public testing::TestWithParam<TestParams> {};

TEST_P(SealPirTest, Works) {
  auto params = GetParam();
  uint32_t rows = params.rows;
  uint32_t row_byte_len = params.row_byte_len;
  bool isSerialized = params.isSerialized;

  // default using d = 2
  SealPirOptions options{params.N, params.rows, params.row_byte_len, 2};

  // Initialize PIR Server
  SPDLOG_INFO("Main: Initializing server and client");
  SealPirClient client(options);

  SealPirServer server(options);

  SPDLOG_INFO("Main: Initializing the database (this may take some time) ...");

  vector<vector<uint8_t>> db_data(rows);
  for (uint64_t i = 0; i < rows; ++i) {
    db_data[i].resize(row_byte_len);
    yacl::crypto::Prg<uint8_t> prg(yacl::crypto::SecureRandU128());
    prg.Fill(absl::MakeSpan(db_data[i]));
  }

  psi::pir_utils::RawDatabase raw_db(std::move(db_data));
  // Measure database setup
  server.SetDatabase(raw_db);
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

  uint64_t ele_index =
      yacl::crypto::RandU64() % rows;  // element in DB at random position
  uint64_t target_raw_idx = ele_index;

  uint64_t pt_idx =
      client.GetPtIndex(target_raw_idx);  // pt_idx of FV plaintext
  uint64_t pt_offset =
      client.GetPtOffset(target_raw_idx);  // pt_offset in FV plaintext
  SPDLOG_INFO("Main: raw_idx = {} from [0, {}]", ele_index, rows - 1);
  SPDLOG_INFO("Main: FV pt_idx = {}, FV pt_offset = {}", pt_idx, pt_offset);

  // Measure query generation
  vector<uint8_t> elems;
  if (isSerialized) {
    yacl::Buffer query_buffer = client.GenerateIndexQuery(target_raw_idx);
    SPDLOG_INFO("Main: query generated");

    yacl::Buffer reply_buffer = server.GenerateIndexResponse(query_buffer);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeIndexResponse(reply_buffer, target_raw_idx);
    SPDLOG_INFO("Main: reply decoded");
  } else {
    SealPir::PirQuery query = client.GenerateQuery(pt_idx);
    SPDLOG_INFO("Main: query generated");

    SealPir::PirReply reply = server.GenerateResponse(query, 0);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeResponse(reply, target_raw_idx);
    SPDLOG_INFO("Main: reply decoded");
  }
  SPDLOG_INFO("Main: query finished");
  EXPECT_EQ(elems.size(), row_byte_len);

  // Check that we retrieved the correct element
  EXPECT_EQ(elems, raw_db.At(ele_index));
  SPDLOG_INFO("Main: PIR result correct!");
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, SealPirTest,
                         testing::Values(
                             // large num items
                             TestParams{4096, 1000000, 256},
                             TestParams{4096, 100000, 256, true},
                             // large value
                             TestParams{4096, 10000, 10241, true},
                             TestParams{4096, 1000, 10240 * 10, true}));
}  // namespace psi::sealpir
