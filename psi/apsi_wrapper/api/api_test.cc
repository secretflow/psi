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

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/apsi_wrapper/api/receiver.h"
#include "psi/apsi_wrapper/api/sender.h"
#include "psi/apsi_wrapper/sender.h"
#include "psi/apsi_wrapper/utils/csv_reader.h"

namespace psi::apsi_wrapper::api {

std::vector<std::string> ReadItems(const std::string& file_path) {
  psi::apsi_wrapper::CSVReader reader(file_path);
  psi::apsi_wrapper::DBData db_data;
  std::vector<std::string> items;

  tie(db_data, items) = reader.read();

  return items;
}

TEST(ApiTest, Works) {
  std::string sender_csv_file = "examples/pir/apsi/data/db.csv";
  std::string params_file = "examples/pir/apsi/parameters/1M-256.json";
  size_t nonce_byte_count = 16;
  bool compress = false;
  std::string receiver_query_file = "examples/pir/apsi/data/query.csv";

  boost::uuids::random_generator uuid_generator;
  auto uuid_str = boost::uuids::to_string(uuid_generator());

  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};

  std::filesystem::create_directories(tmp_folder);

  std::string receiver_output_file = tmp_folder / "result.csv";
  std::string receiver_output_file_2 = tmp_folder / "result.csv";
  std::string sdb_out_file = tmp_folder / "sdb_out";
  std::string bucket_senderdb_folder = tmp_folder / "bucket_senderdb";

  const size_t bucket_cnt = 10;

  EXPECT_TRUE(std::filesystem::create_directory(bucket_senderdb_folder));

  {
    psi::apsi_wrapper::api::Sender sender;
    sender.LoadCsv(sender_csv_file, params_file, nonce_byte_count, compress);

    psi::apsi_wrapper::api::Receiver receiver;
    receiver.LoadParamsConfig(params_file);
    receiver.LoadItems(receiver_query_file);

    std::string oprf_request_str = receiver.RequestOPRF();
    std::string oprf_response_str = sender.RunOPRF(oprf_request_str);

    std::string query_request_str = receiver.RequestQuery(oprf_response_str);
    std::string query_response_str = sender.RunQuery(query_request_str);

    receiver.ProcessResult(query_response_str, receiver_output_file);

    ASSERT_TRUE(sender.SaveSenderDb(sdb_out_file));
  }

  {
    psi::apsi_wrapper::api::Sender sender;
    ASSERT_TRUE(sender.LoadSenderDb(sdb_out_file));
    std::string params_str = sender.GenerateParams();

    psi::apsi_wrapper::api::Receiver receiver;
    receiver.LoadSenderParams(params_str);
    receiver.LoadItems(receiver_query_file);

    std::string oprf_request_str = receiver.RequestOPRF();
    std::string oprf_response_str = sender.RunOPRF(oprf_request_str);

    std::string query_request_str = receiver.RequestQuery(oprf_response_str);
    std::string query_response_str = sender.RunQuery(query_request_str);

    receiver.ProcessResult(query_response_str, receiver_output_file_2);
  }

  {
    psi::apsi_wrapper::api::Sender::SaveBucketizedSenderDb(
        sender_csv_file, params_file, nonce_byte_count, compress,
        bucket_senderdb_folder, bucket_cnt);

    psi::apsi_wrapper::api::Sender sender;
    sender.LoadBucketizedSenderDb(bucket_senderdb_folder, bucket_cnt);
    std::string params_str = sender.GenerateParams();

    psi::apsi_wrapper::api::Receiver receiver;
    receiver.LoadSenderParams(params_str);

    std::vector<std::pair<size_t, std::vector<std::string>>> buckets =
        receiver.BucketizeItems(receiver_query_file, bucket_cnt);

    bool append_to_outfile = false;

    for (const auto& [k, v] : buckets) {
      receiver.LoadItems(v);

      std::string oprf_request_str = receiver.RequestOPRF(k);
      std::string oprf_response_str = sender.RunOPRF(oprf_request_str);

      std::string query_request_str =
          receiver.RequestQuery(oprf_response_str, k);
      std::string query_response_str = sender.RunQuery(query_request_str);

      size_t cnt = receiver.ProcessResult(
          query_response_str, receiver_output_file_2, append_to_outfile);

      if (cnt > 0 && !append_to_outfile) {
        append_to_outfile = true;
      }
    }
  }

  std::vector<std::string> query_items = ReadItems(receiver_query_file);
  std::vector<std::string> output_items_1 = ReadItems(receiver_output_file);
  std::vector<std::string> output_items_2 = ReadItems(receiver_output_file_2);

  std::sort(query_items.begin(), query_items.end());
  std::sort(output_items_1.begin(), output_items_1.end());
  std::sort(output_items_2.begin(), output_items_2.end());

  EXPECT_EQ(output_items_1, query_items);
  EXPECT_EQ(output_items_2, query_items);

  {
    std::error_code ec;
    std::filesystem::remove_all(tmp_folder, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                  tmp_folder.string(), ec.message());
    }
  }
}

}  // namespace psi::apsi_wrapper::api
