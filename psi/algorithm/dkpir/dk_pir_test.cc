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

#include <filesystem>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/algorithm/dkpir/entry.h"
#include "psi/utils/random_str.h"

namespace psi::dkpir {

std::unordered_set<std::string> ReadCsvRow(const std::string& file_path) {
  std::unordered_set<std::string> lines;
  std::ifstream file(file_path);
  std::string line;
  while (std::getline(file, line)) {
    lines.insert(line);
  }
  return lines;
}

TEST(DkPirTest, Works) {
  CurveType curve_type = CurveType::CURVE_FOURQ;

  std::string sender_source_file =
      "examples/pir/apsi/data/duplicate_key_db.csv";
  std::string params_file = "examples/pir/apsi/parameters/100-1-300.json";
  std::string receiver_query_file =
      "examples/pir/apsi/data/duplicate_key_query.csv";
  std::string target_file =
      "examples/pir/apsi/data/duplicate_key_target_result.csv";

  auto uuid_str = GetRandomString();
  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};
  std::filesystem::create_directories(tmp_folder);

  std::string value_sdb_file = tmp_folder / "value_sdb_out.db";
  std::string count_sdb_file = tmp_folder / "count_sdb_out.db";
  std::string secret_key_file = tmp_folder / "secret_key.key";
  std::string receiver_output_file = tmp_folder / "result.csv";
  std::string sender_output_file = tmp_folder / "row_count.csv";

  std::string key = "id";
  std::vector<std::string> labels = {"label1", "label2", "label3"};

  DkPirSenderOptions sender_options;
  sender_options.curve_type = curve_type;
  sender_options.params_file = params_file;
  sender_options.source_file = sender_source_file;
  sender_options.value_sdb_out_file = value_sdb_file;
  sender_options.count_sdb_out_file = count_sdb_file;
  sender_options.secret_key_file = secret_key_file;
  sender_options.result_file = sender_output_file;
  sender_options.tmp_folder = tmp_folder.string();
  sender_options.streaming_result = false;
  sender_options.key = key;
  sender_options.labels = labels;

  DkPirReceiverOptions receiver_options;
  receiver_options.curve_type = curve_type;
  receiver_options.params_file = params_file;
  receiver_options.query_file = receiver_query_file;
  receiver_options.result_file = receiver_output_file;
  receiver_options.tmp_folder = tmp_folder.string();
  receiver_options.streaming_result = false;
  receiver_options.key = key;
  receiver_options.labels = labels;

  // In this case, the sender will not check the count of the query result.
  {
    sender_options.skip_count_check = true;
    receiver_options.skip_count_check = true;

    const int kWorldSize = 2;
    auto contexts = yacl::link::test::SetupWorld(kWorldSize);

    SenderOffline(sender_options);

    std::future<int> sender = std::async(std::launch::async, SenderOnline,
                                         std::ref(sender_options), contexts[0]);

    std::future<int> receiver =
        std::async(std::launch::async, ReceiverOnline,
                   std::ref(receiver_options), contexts[1]);

    sender.get();
    receiver.get();

    std::unordered_set<std::string> target_data = ReadCsvRow(target_file);
    std::unordered_set<std::string> result_data =
        ReadCsvRow(receiver_output_file);

    EXPECT_EQ(target_data, result_data);
  }

  // In this case, the sender will check the count of the query result.
  {
    sender_options.skip_count_check = false;
    receiver_options.skip_count_check = false;

    const int kWorldSize = 2;
    auto contexts = yacl::link::test::SetupWorld(kWorldSize);

    SenderOffline(sender_options);

    std::future<int> sender = std::async(std::launch::async, SenderOnline,
                                         std::ref(sender_options), contexts[0]);

    std::future<int> receiver =
        std::async(std::launch::async, ReceiverOnline,
                   std::ref(receiver_options), contexts[1]);

    sender.get();
    receiver.get();

    std::unordered_set<std::string> target_data = ReadCsvRow(target_file);
    std::unordered_set<std::string> result_data =
        ReadCsvRow(receiver_output_file);

    EXPECT_EQ(target_data, result_data);
  }

  {
    std::error_code ec;
    std::filesystem::remove_all(tmp_folder, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                  tmp_folder.string(), ec.message());
    }
  }
}

}  // namespace psi::dkpir