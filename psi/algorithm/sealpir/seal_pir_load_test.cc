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

#include <seal/seal.h>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/sealpir/seal_pir.h"

using namespace std;
using namespace seal;

namespace psi::sealpir {

namespace {
struct TestParams {
  uint32_t N = 4096;
  uint64_t rows;
  uint64_t row_byte_len = 256;
};
}  // namespace

class SealPirLoadTest : public testing::TestWithParam<TestParams> {};

TEST_P(SealPirLoadTest, Works) {
  // get a test params
  auto params = GetParam();
  uint32_t rows = params.rows;
  uint32_t row_byte_len = params.row_byte_len;

  // default using d = 2
  SealPirOptions options{params.N, rows, row_byte_len, 2};

  // Initialize PIR Server
  SPDLOG_INFO("Initializing server and client");
  SealPirClient client(options);
  SealPirServer tmp_server(options);
  psi::pir::IndexPirDataBase& tmp_db = tmp_server;

  yacl::ElapsedTimer timer;

  // generate random database
  vector<vector<uint8_t>> db_data(rows);
  for (uint64_t i = 0; i < rows; ++i) {
    db_data[i].resize(row_byte_len);
    yacl::crypto::Prg<uint8_t> prg(yacl::crypto::SecureRandU128());
    prg.Fill(absl::MakeSpan(db_data[i]));
  }
  psi::pir::RawDatabase raw_db(std::move(db_data));
  SPDLOG_INFO("Gen random database, time cost: {} ms", timer.CountMs());

  //  server set database
  timer.Restart();
  tmp_db.GenerateFromRawData(raw_db);
  SPDLOG_INFO("Server set database, time cost: {} ms", timer.CountMs());

  yacl::Buffer galois_keys_str = client.GeneratePksBuffer();
  // using the API provided by SealPirServer to set galois keys for client 0
  tmp_server.SetGaloisKey(0, galois_keys_str);
  SPDLOG_INFO("Set Galois keys");

  // dump and load the server
  timer.Restart();
  std::string file_name = "tmp_seal_pir_server.bin";
  std::ofstream output(file_name, std::ios::binary);
  tmp_db.Dump(output);
  output.close();
  SPDLOG_INFO("Server dump to file, time cost: {} ms", timer.CountMs());

  timer.Restart();
  // using the API provided by PirDatabase
  int fd = open(file_name.c_str(), O_RDONLY);
  google::protobuf::io::FileInputStream input(fd);
  auto server = SealPirServer::Load(input);
  SPDLOG_INFO("Server loaded, time cost: {} ms", timer.CountMs());

  // gen random query index
  uint64_t ele_index =
      yacl::crypto::RandU64() % rows;  // element in DB at random position
  uint64_t target_raw_idx = ele_index;

  uint64_t pt_idx =
      client.GetPtIndex(target_raw_idx);  // pt_idx of FV plaintext
  uint64_t pt_offset =
      client.GetPtOffset(target_raw_idx);  // pt_offset in FV plaintext
  SPDLOG_INFO("raw_idx = {} from [0, {}]", ele_index, rows - 1);
  SPDLOG_INFO("FV pt_idx = {}, FV pt_offset = {}", pt_idx, pt_offset);

  // query and response
  std::vector<uint8_t> elems;

  timer.Restart();
  yacl::Buffer query_buffer = client.GenerateIndexQuery(target_raw_idx);
  SPDLOG_INFO("Query generated, time cost: {} ms", timer.CountMs());

  timer.Restart();
  yacl::Buffer reply_buffer = server->Response(query_buffer, galois_keys_str);
  SPDLOG_INFO("Response generated, time cost: {} ms", timer.CountMs());

  timer.Restart();
  elems = client.DecodeIndexResponse(reply_buffer, target_raw_idx);
  SPDLOG_INFO("Response decoded, time cost: {} ms", timer.CountMs());

  SPDLOG_INFO("query finished");
  EXPECT_EQ(elems.size(), row_byte_len);

  // Check that we retrieved the correct element
  EXPECT_EQ(elems, raw_db.At(ele_index));
  SPDLOG_INFO("PIR result correct!");
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, SealPirLoadTest,
                         testing::Values(
                             // large num items
                             TestParams{4096, 100000, 256}  // large value
                             //  TestParams{4096, 10000, 10241},
                             //  TestParams{4096, 1000, 10240 * 10}

                             ));
}  // namespace psi::sealpir