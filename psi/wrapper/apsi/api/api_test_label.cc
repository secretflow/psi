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

psi::apsi_wrapper::LabeledData ReadLabeledDB(const std::string& file_path) {
  psi::apsi_wrapper::ApsiCsvReader reader(file_path);
  psi::apsi_wrapper::DBData db_data;
  std::vector<std::string> items;
  tie(db_data, items) = reader.read();
  // get LabeledData
  return std::get<psi::apsi_wrapper::LabeledData>(db_data);
}

TEST(ApiTest, Works) {
  std::string sender_csv_file = "examples/pir/apsi/data/db_100_300byte.csv";
  std::string params_file = "examples/pir/apsi/parameters/100-1-300.json";
  size_t nonce_byte_count = 16;
  const size_t bucket_cnt = 10;
  bool compress = false;
  std::string receiver_query_file =
      "examples/pir/apsi/data/query_1_100_300byte.csv";

  std::filesystem::path tmp_dir = std::filesystem::temp_directory_path();

  auto uuid_str = GetRandomString();

  std::string receiver_output_file =
      tmp_dir / fmt::format("result_{}.csv", uuid_str);
  std::string receiver_output_file_2 =
      tmp_dir / fmt::format("result_{}_2.csv", uuid_str);
  std::string sdb_out_file = tmp_dir / fmt::format("out_sdb_{}.csv", uuid_str);

  {
    psi::apsi_wrapper::api::Sender::Option sender_option;
    sender_option.source_file = sender_csv_file;
    sender_option.params_file = params_file;
    sender_option.nonce_byte_count = nonce_byte_count;
    sender_option.compress = compress;
    sender_option.db_path = sdb_out_file;
    sender_option.num_buckets = bucket_cnt;
    sender_option.group_cnt = bucket_cnt;

    psi::apsi_wrapper::api::Sender sender(sender_option);

    sender.GenerateSenderDb();

    std::string params_str = sender.GenerateParams();

    psi::apsi_wrapper::api::Receiver receiver(bucket_cnt);

    receiver.LoadParamsConfig(params_file);
    auto recv_context = receiver.BucketizeItems(receiver_query_file);

    auto oprf_requst = receiver.RequestOPRF(recv_context);

    auto oprf_response = sender.RunOPRF(oprf_requst);

    auto query_request = receiver.RequestQuery(recv_context, oprf_response);

    auto query_response = sender.RunQuery(query_request);

    auto cnts = receiver.ProcessResult(recv_context, query_response,
                                       receiver_output_file);
  }
  psi::apsi_wrapper::LabeledData sender_labeled_data =
      ReadLabeledDB(sender_csv_file);

  std::vector<std::string> query_items = ReadItems(receiver_query_file);

  psi::apsi_wrapper::LabeledData out_labeled_db_1 =
      ReadLabeledDB(receiver_output_file);

  // travrse query_item and get the related label
  for (auto& item_str : query_items) {
    apsi::Item item(item_str);
    apsi::Label real_label;
    apsi::Label query_label_1;
    apsi::Label query_label_2;
    // ground truth
    auto it = std::find_if(
        sender_labeled_data.begin(), sender_labeled_data.end(),
        [&item](const std::pair<::apsi::Item, ::apsi::Label>& entry) {
          return entry.first == item;
        });
    if (it != sender_labeled_data.end()) {
      real_label = it->second;
    } else {
      continue;
    }
    // query result 1
    auto it_1 = std::find_if(
        out_labeled_db_1.begin(), out_labeled_db_1.end(),
        [&item](const std::pair<::apsi::Item, ::apsi::Label>& entry) {
          return entry.first == item;
        });
    if (!(it_1 != out_labeled_db_1.end())) {
      throw std::runtime_error("Item not found in out_labeled_db_1");
    }
    query_label_1 = it_1->second;
    EXPECT_EQ(real_label, query_label_1);
    //  query result 2
  }

  {
    std::error_code ec;
    std::filesystem::remove_all(receiver_output_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file: {}, msg: {}", receiver_output_file,
                  ec.message());
    }
  }

  {
    std::error_code ec;
    std::filesystem::remove_all(sdb_out_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file: {}, msg: {}", sdb_out_file,
                  ec.message());
    }
  }
}

}  // namespace psi::apsi_wrapper::api
