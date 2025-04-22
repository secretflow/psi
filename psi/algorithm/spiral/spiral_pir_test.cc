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

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/spiral/gadget.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/spiral_client.h"
#include "psi/algorithm/spiral/spiral_server.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {

struct TestParams {
  size_t database_rows_ = 0;
  size_t database_row_byte_ = 0;
  bool serialized_ = false;
};

}  // namespace

class SpiralPirTest : public testing::TestWithParam<TestParams> {};

TEST_P(SpiralPirTest, Works) {
  // get a test params
  auto test_param = GetParam();
  auto database_rows = test_param.database_rows_;
  auto row_byte = test_param.database_row_byte_;
  auto serialized = test_param.serialized_;
  DatabaseMetaInfo database_info(database_rows, row_byte);

  SPDLOG_INFO("database rows: {}, row bytes: {}", database_rows, row_byte);

  yacl::ElapsedTimer timer;

  // Gen database
  SPDLOG_INFO("GenRandomDatabase, this will take much time");
  psi::pir::RawDatabase raw_database =
      psi::pir::RawDatabase::Random(database_rows, row_byte);
  SPDLOG_INFO("GenRandomDatabase, time cost: {} ms", timer.CountMs());

  // get a SpiralParams
  auto spiral_params = util::GetDefaultParam();
  spiral_params.UpdateByDatabaseInfo(database_info);

  SPDLOG_INFO("MaxByteLenofPt: {}, MaxBitLenOfPt: {}",
              spiral_params.MaxByteLenOfPt(), spiral_params.MaxBitLenOfPt());

  Params params_client(spiral_params);
  Params params_server(spiral_params);

  SPDLOG_INFO("Spiral Params: \n{}", spiral_params.ToString());

  // new client
  SpiralClient client(std::move(params_client), database_info);

  // new Server
  SpiralServer server(std::move(params_server), database_info);

  // set database
  timer.Restart();
  server.GenerateFromRawDataAndReorient(raw_database);
  SPDLOG_INFO("Server SetDatabase, time cost: {} ms", timer.CountMs());

  // gen random target_idx
  std::random_device rd;
  std::mt19937_64 rng(rd());
  size_t raw_idx_target = rng() % database_rows;
  std::vector<uint8_t> correct_row(raw_database.At(raw_idx_target));

  yacl::ElapsedTimer timer2;

  // query and response
  if (!serialized) {
    auto pp = client.GenPublicKeys();
    SPDLOG_INFO("Do query without serialized");

    // set public paramter
    server.SetPublicKeys(std::move(pp));

    // now generate Query
    timer2.Restart();
    timer.Restart();
    auto query = client.GenQuery(raw_idx_target);
    SPDLOG_INFO("Client GenQuery, time cost: {} ms", timer.CountMs());

    // server handle query
    timer.Restart();
    auto responses = server.ProcessQuery(query);
    SPDLOG_INFO("Server ProcessQuery, time cost: {} ms", timer.CountMs());

    timer.Restart();
    auto decode = client.DecodeResponse(responses, raw_idx_target);
    SPDLOG_INFO("Client DecodeResponse, time cost: {} ms", timer.CountMs());

    // verify
    EXPECT_EQ(correct_row, decode);

  } else {
    SPDLOG_INFO("Do query with serialized");

    timer.Restart();
    auto pks_buffer = client.GeneratePksBuffer();
    SPDLOG_INFO("public params gen time cost {} ms", timer.CountMs());
    SPDLOG_INFO("public params serialize size {} kb", pks_buffer.size() / 1024);

    timer2.Restart();
    timer.Restart();
    auto query_buffer = client.GenerateIndexQuery(raw_idx_target);

    SPDLOG_INFO("Client GenQuery, time cost: {} ms", timer.CountMs());
    SPDLOG_INFO("query serialize size {} kb", query_buffer.size() / 1024);

    timer.Restart();
    auto response_buffer = server.Response(query_buffer, pks_buffer);
    SPDLOG_INFO("Server ProcessQuery, time cost: {} ms", timer.CountMs());
    SPDLOG_INFO("response serialize size {} kb", response_buffer.size() / 1024);

    // client decode
    timer.Restart();
    auto decode = client.DecodeIndexResponse(response_buffer, raw_idx_target);
    SPDLOG_INFO("Client DecodeResponse, time cost: {} ms", timer.CountMs());

    // verify
    EXPECT_EQ(correct_row, decode);
  }

  SPDLOG_INFO("database rows: {}, row bytes: {}", database_rows, row_byte);
  SPDLOG_INFO("One time query ,total time: {} ms", timer2.CountMs());
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, SpiralPirTest,
                         testing::Values(TestParams{10000, 256},
                                         TestParams{100000, 256},
                                         TestParams{1000000, 256}
                                         // large value
                                         //  TestParams{1000, 8193, true},
                                         //  TestParams{100, 8192 * 10, true}

                                         ));

}  // namespace psi::spiral
