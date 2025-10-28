// Copyright 2025
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

#include <fstream>
#include <string>
#include <vector>

#include "experiment/psi/threshold_ub_psi/client.h"
#include "experiment/psi/threshold_ub_psi/server.h"
#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/utils/io.h"
#include "psi/utils/random_str.h"
#include "psi/utils/test_utils.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::ecdh {

namespace {

std::unordered_set<std::string> ReadCsvRow(const std::string& file_path) {
  std::unordered_set<std::string> lines;
  std::ifstream file(file_path);
  std::string line;
  std::getline(file, line);
  while (std::getline(file, line)) {
    lines.insert(line);
  }
  return lines;
}

}  // namespace

struct TestParams {
  std::vector<std::string> items_server;
  std::vector<std::string> items_client;
  uint32_t threshold;
};

class ThresholdEcdhUbPsiTest : public ::testing::TestWithParam<TestParams> {};

TEST_P(ThresholdEcdhUbPsiTest, Works) {
  auto params = GetParam();
  auto ctxs = yacl::link::test::SetupWorld(2);

  std::string uuid_str = GetRandomString();
  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};
  std::filesystem::create_directories(tmp_folder);

  std::string server_output_path = tmp_folder / "server_output.csv";
  std::string client_output_path = tmp_folder / "client_output.csv";

  v2::UbPsiConfig server_config;
  v2::UbPsiConfig client_config;

  test::GenerateUbPsiConfig(tmp_folder, params.items_server,
                            params.items_client, params.threshold,
                            server_config, client_config);

  auto proc_server = [&](const v2::UbPsiConfig& ub_psi_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
    ThresholdEcdhUbPsiServer server(ub_psi_config, lctx);
    server.Run();
  };

  auto proc_client = [&](const v2::UbPsiConfig& ub_psi_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
    ThresholdEcdhUbPsiClient client(ub_psi_config, lctx);
    client.Run();
  };

  std::future<void> fa = std::async(proc_server, server_config, ctxs[0]);
  std::future<void> fb = std::async(proc_client, client_config, ctxs[1]);

  fa.get();
  fb.get();

  std::vector<std::string> real_intersection =
      test::GetIntersection(params.items_server, params.items_client);

  std::unordered_set<std::string> result_server =
      ReadCsvRow(server_output_path);
  std::unordered_set<std::string> result_client =
      ReadCsvRow(client_output_path);

  // Verify that the intersection of both parties is consistent
  EXPECT_EQ(result_server, result_client);

  // Verify that the restricted intersection is a subset of the real
  // intersection
  std::sort(real_intersection.begin(), real_intersection.end());

  for (auto& item : result_server) {
    EXPECT_TRUE(std::binary_search(real_intersection.begin(),
                                   real_intersection.end(), item));
  }
  // Verify that the size of the restricted intersection meets expectations
  uint32_t real_intersection_count = real_intersection.size();
  uint32_t target_size = std::min(real_intersection_count, params.threshold);
  EXPECT_EQ(result_server.size(), target_size);

  psi::ResourceManager::GetInstance().RemoveAllResource();

  {
    std::error_code ec;
    std::filesystem::remove_all(tmp_folder, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                  tmp_folder.string(), ec.message());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, ThresholdEcdhUbPsiTest,
    testing::Values(TestParams{{"a", "b"}, {"b", "c"}, 1},   //
                    TestParams{{"a", "b"}, {"b", "c"}, 10},  //
                    TestParams{{"a", "b"}, {"c", "d"}, 1},   //
                    TestParams{{"a", "b"}, {"c", "d"}, 10},  //
                    TestParams{test::CreateRangeItems(0, 4095),
                               test::CreateRangeItems(1, 4095), 100},  //
                    TestParams{test::CreateRangeItems(0, 4095),
                               test::CreateRangeItems(1, 4095), 1000},  //
                    TestParams{test::CreateRangeItems(0, 4095),
                               test::CreateRangeItems(1, 4095), 5000},  //
                    TestParams{test::CreateRangeItems(0, 4096),
                               test::CreateRangeItems(1, 4096), 100},  //
                    TestParams{test::CreateRangeItems(0, 4096),
                               test::CreateRangeItems(1, 4096), 1000},  //
                    TestParams{test::CreateRangeItems(0, 4096),
                               test::CreateRangeItems(1, 4096), 5000},  //
                    TestParams{test::CreateRangeItems(0, 10000),
                               test::CreateRangeItems(1, 10000), 100},  //
                    TestParams{test::CreateRangeItems(0, 10000),
                               test::CreateRangeItems(1, 10000), 1000},  //
                    TestParams{test::CreateRangeItems(0, 10000),
                               test::CreateRangeItems(1, 10000), 10000}  //
                    ));

}  // namespace psi::ecdh