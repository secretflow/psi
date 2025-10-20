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

#include "experiment/psi/threshold_ecdh_psi/common.h"
#include "experiment/psi/threshold_ecdh_psi/receiver.h"
#include "experiment/psi/threshold_ecdh_psi/sender.h"
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
  std::vector<std::string> items_sender;
  std::vector<std::string> items_receiver;
  uint32_t threshold;
};

class ThresholdEcdhPsiTest : public ::testing::TestWithParam<TestParams> {};

TEST_P(ThresholdEcdhPsiTest, Works) {
  auto params = GetParam();
  auto ctxs = yacl::link::test::SetupWorld(2);

  std::string uuid_str = GetRandomString();
  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};
  std::filesystem::create_directories(tmp_folder);

  std::string sender_output_path = tmp_folder / "sender_output.csv";
  std::string receiver_output_path = tmp_folder / "receiver_output.csv";

  v2::PsiConfig sender_config;
  v2::PsiConfig receiver_config;

  GeneratePsiConfig(tmp_folder, params.items_sender, params.items_receiver,
                    params.threshold, sender_config, receiver_config);

  auto proc_sender = [&](const v2::PsiConfig& psi_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
    ThresholdEcdhPsiSender sender(psi_config, lctx);
    sender.Run();
  };

  auto proc_receiver = [&](const v2::PsiConfig& psi_config,
                           const std::shared_ptr<yacl::link::Context>& lctx) {
    ThresholdEcdhPsiReceiver receiver(psi_config, lctx);
    receiver.Run();
  };

  std::future<void> fa = std::async(proc_sender, sender_config, ctxs[0]);
  std::future<void> fb = std::async(proc_receiver, receiver_config, ctxs[1]);

  fa.get();
  fb.get();

  std::vector<std::string> real_intersection =
      test::GetIntersection(params.items_sender, params.items_receiver);

  std::unordered_set<std::string> result_sender =
      ReadCsvRow(sender_output_path);
  std::unordered_set<std::string> result_receiver =
      ReadCsvRow(receiver_output_path);

  // Verify that the intersection of both parties is consistent
  EXPECT_EQ(result_sender, result_receiver);

  // Verify that the restricted intersection is a subset of the real
  // intersection
  std::sort(real_intersection.begin(), real_intersection.end());
  for (auto& item : result_sender) {
    EXPECT_TRUE(std::binary_search(real_intersection.begin(),
                                   real_intersection.end(), item));
  }

  // Verify that the size of the restricted intersection meets expectations
  uint32_t real_intersection_count = real_intersection.size();
  uint32_t target_size = std::min(real_intersection_count, params.threshold);
  EXPECT_EQ(result_sender.size(), target_size);

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
    Works_Instances, ThresholdEcdhPsiTest,
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