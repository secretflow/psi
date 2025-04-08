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

#include "psi/algorithm/dkpir/test/receiver.h"
#include "psi/algorithm/dkpir/test/sender.h"

namespace psi::dkpir::test {

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
  std::string receiver_tmp_query_file = tmp_folder / "tmp_query.csv";
  std::string receiver_tmp_output_file = tmp_folder / "tmp_output.csv";

  std::string key = "id";
  std::vector<std::string> labels = {"label1", "label2", "label3"};

  {
    psi::dkpir::DkPirSenderOptions sender_options;
    sender_options.curve_type = curve_type;
    sender_options.params_file = params_file;
    sender_options.source_file = sender_source_file;
    sender_options.value_sdb_out_file = value_sdb_file;
    sender_options.count_sdb_out_file = count_sdb_file;
    sender_options.secret_key_file = secret_key_file;
    sender_options.tmp_folder = tmp_folder.string();
    sender_options.key = key;
    sender_options.labels = labels;

    psi::dkpir::test::Sender sender(sender_options);

    psi::dkpir::test::Receiver receiver;

    sender.LoadSenderDB(value_sdb_file, count_sdb_file, params_file);
    sender.LoadSecretKey(curve_type, secret_key_file);

    receiver.LoadParams(params_file);

    std::vector<::apsi::Item> items = receiver.ExtractItems(
        receiver_query_file, receiver_tmp_query_file, key);

    std::string oprf_request = receiver.RequestOPRF(items);

    std::string oprf_response = sender.RunOPRF(oprf_request);

    std::string query_request = receiver.RequestQuery(oprf_response);

    std::string count_query_response = sender.RunQuery(query_request, true);

    std::vector<::apsi::receiver::MatchRecord> count_query_result =
        receiver.ProcessQueryResponse(count_query_response);

    heu::lib::algorithms::elgamal::Ciphertext row_count_ct =
        receiver.ComputeRowCountCt(curve_type, count_query_result);

    std::string value_query_response = sender.RunQuery(query_request, false);

    std::vector<::apsi::receiver::MatchRecord> value_query_result =
        receiver.ProcessQueryResponse(value_query_response);

    uint64_t row_count = receiver.ComputeRowCount(value_query_result);

    EXPECT_TRUE(sender.CheckRowCount(row_count_ct, row_count));

    uint128_t shuffle_seed = sender.GetShuffleSeed();
    uint64_t shuffle_counter = sender.GetShuffleCounter();

    receiver.SaveResult(value_query_result, items, shuffle_seed,
                        shuffle_counter, receiver_output_file,
                        receiver_tmp_output_file, key, labels);

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
}  // namespace psi::dkpir::test