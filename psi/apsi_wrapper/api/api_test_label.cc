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

psi::apsi_wrapper::LabeledData ReadLabeledDB(const std::string& file_path) {
  psi::apsi_wrapper::CSVReader reader(file_path);
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
  bool compress = false;
  std::string receiver_query_file =
      "examples/pir/apsi/data/query_1_100_300byte.csv";

  std::filesystem::path tmp_dir = std::filesystem::temp_directory_path();

  boost::uuids::random_generator uuid_generator;
  auto uuid_str = boost::uuids::to_string(uuid_generator());

  std::string receiver_output_file =
      tmp_dir / fmt::format("result_{}.csv", uuid_str);
  std::string receiver_output_file_2 =
      tmp_dir / fmt::format("result_{}_2.csv", uuid_str);
  std::string sdb_out_file = tmp_dir / fmt::format("out_sdb_{}.csv", uuid_str);

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

    sender.SaveSenderDb(sdb_out_file);
  }

  {
    psi::apsi_wrapper::api::Sender sender;
    sender.LoadSenderDb(sdb_out_file);
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

  psi::apsi_wrapper::LabeledData sender_labeled_data =
      ReadLabeledDB(sender_csv_file);

  std::vector<std::string> query_items = ReadItems(receiver_query_file);

  psi::apsi_wrapper::LabeledData out_labeled_db_1 =
      ReadLabeledDB(receiver_output_file);
  psi::apsi_wrapper::LabeledData out_labeled_db_2 =
      ReadLabeledDB(receiver_output_file_2);

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
    auto it_2 = std::find_if(
        out_labeled_db_2.begin(), out_labeled_db_2.end(),
        [&item](const std::pair<::apsi::Item, ::apsi::Label>& entry) {
          return entry.first == item;
        });
    if (!(it_2 != out_labeled_db_2.end())) {
      throw std::runtime_error("Item not found in out_labeled_db_2");
    }
    query_label_2 = it_2->second;
    EXPECT_EQ(real_label, query_label_2);
  }

  {
    std::error_code ec;
    std::filesystem::remove(receiver_output_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file: {}, msg: {}", receiver_output_file,
                  ec.message());
    }
  }
  {
    std::error_code ec;
    std::filesystem::remove(receiver_output_file_2, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file: {}, msg: {}",
                  receiver_output_file_2, ec.message());
    }
  }
  {
    std::error_code ec;
    std::filesystem::remove(sdb_out_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file: {}, msg: {}", sdb_out_file,
                  ec.message());
    }
  }
}

}  // namespace psi::apsi_wrapper::api
