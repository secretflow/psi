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
#include "psi/wrapper/apsi/api/receiver_c_wrapper.h"
#include "psi/wrapper/apsi/api/sender_c_wrapper.h"
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
  const size_t group_cnt = 10;

  EXPECT_TRUE(std::filesystem::create_directory(bucket_senderdb_folder));

  {
    Sender* sender = BucketSenderMake(
        sender_csv_file.c_str(), sdb_out_file.c_str(), group_cnt, bucket_cnt,
        nonce_byte_count, params_file.c_str(), compress);

    CString params_str = BucketSenderGenerateParams(sender);

    Receiver* receiver = BucketReceiverMake(bucket_cnt, 8);

    BucketReceiverLoadParamsStr(receiver, params_str);

    QueryCtx* query_ctx = BucketReceiverMakeQueryCtxFromFile(
        receiver, receiver_query_file.c_str());

    CStringArray oprf_requst = BucketReceiverRequestOPRF(receiver, query_ctx);

    CStringArray oprf_response = BucketSenderRunOPRFBatch(sender, oprf_requst);

    CStringArray query_request =
        BucketReceiverRequestQuery(receiver, query_ctx, oprf_response);

    CStringArray query_response =
        BucketSenderRunQueryBatch(sender, query_request);

    uint32_t cnts = BucketReceiverProcessResultToFile(
        receiver, query_ctx, query_response, receiver_output_file.c_str());

    {
      FreeCStringArray(&query_response);
      FreeCStringArray(&query_request);
      FreeCStringArray(&oprf_response);
      FreeCStringArray(&oprf_requst);
      FreeCString(&params_str);
      BucketReceiverFreeQueryCtx(query_ctx);
      BucketSenderFree(&sender);
      BucketReceiverFree(&receiver);
    }

    // TEST result
    std::vector<std::string> query_items = ReadItems(receiver_query_file);
    std::vector<std::string> output_items_1 = ReadItems(receiver_output_file);

    std::sort(query_items.begin(), query_items.end());
    std::sort(output_items_1.begin(), output_items_1.end());

    EXPECT_EQ(output_items_1, query_items);
    EXPECT_EQ(output_items_1.size(), cnts);

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
