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

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/utils/random_str.h"
#include "psi/wrapper/apsi/api/receiver.h"
#include "psi/wrapper/apsi/api/sender.h"
#include "psi/wrapper/apsi/sender.h"
#include "psi/wrapper/apsi/utils/csv_reader.h"

namespace psi::apsi_wrapper::api {

std::vector<std::string> ReadItems(const std::string& file_path) {
  psi::apsi_wrapper::ApsiCsvReader reader(file_path);
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

  auto uuid_str = GetRandomString();

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
    psi::apsi_wrapper::api::Sender::Option sender_option;
    sender_option.source_file = sender_csv_file;
    sender_option.params_file = params_file;
    sender_option.nonce_byte_count = nonce_byte_count;
    sender_option.compress = compress;
    sender_option.db_path = sdb_out_file;
    sender_option.num_buckets = bucket_cnt;
    sender_option.group_cnt = bucket_cnt;

    SPDLOG_INFO("test bucketized sender db");

    psi::apsi_wrapper::api::Sender sender(sender_option);
    sender.GenerateSenderDb();

    std::string params_str = sender.GenerateParams();

    psi::apsi_wrapper::api::Receiver receiver(bucket_cnt);

    receiver.LoadSenderParams(params_str);

    auto recv_context = receiver.BucketizeItems(receiver_query_file);

    auto oprf_requst = receiver.RequestOPRF(recv_context);

    auto oprf_response = sender.RunOPRF(oprf_requst);

    auto query_request = receiver.RequestQuery(recv_context, oprf_response);

    auto query_response = sender.RunQuery(query_request);

    auto cnts = receiver.ProcessResult(recv_context, query_response,
                                       receiver_output_file);

    std::vector<std::string> query_items = ReadItems(receiver_query_file);
    std::vector<std::string> output_items_1 = ReadItems(receiver_output_file);

    std::sort(query_items.begin(), query_items.end());
    std::sort(output_items_1.begin(), output_items_1.end());

    EXPECT_EQ(output_items_1, query_items);

    {
      std::error_code ec;
      std::filesystem::remove_all(tmp_folder, ec);
      if (ec.value() != 0) {
        SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                    tmp_folder.string(), ec.message());
      }
    }
  }
}

}  // namespace psi::apsi_wrapper::api
