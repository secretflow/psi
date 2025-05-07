// Copyright 2025 Ant Group Co., Ltd.
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

#include "psi/utils/join_processor.h"

#include <filesystem>

#include "gtest/gtest.h"
#include "index_store.h"
#include "random_str.h"
#include "table_utils.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

constexpr auto csv_content = R"csv("id","label1","label2","label3","label4"
1,"b","y1",0.12,"one"
3,"c","y3",0.9,"three"
4,"b","y4",-12,"four"
1,"b","y1",-0.13,"two"
)csv";

constexpr auto unique_csv_content =
    R"csv("id","label1","label2","label3","label4"
1,"b","y1",0.12,"one"
3,"c","y3",0.9,"three"
4,"b","y4",-12,"four"
)csv";

size_t GetFileLine(const std::string& path) {
  std::ifstream ifs(path);
  std::string line;
  size_t count = 0;
  while (std::getline(ifs, line)) {
    ++count;
  }
  return count;
}

class JoinProcessorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() / GetRandomString();
    std::filesystem::create_directories(root_);
    csv_path_ = (root_ / "test.csv").string();
    std::ofstream csv_of(csv_path_);
    csv_of << csv_content;
    csv_of.close();
    unique_csv_path_ = (root_ / "unique_test.csv").string();
    std::ofstream unique_csv_of(unique_csv_path_);
    unique_csv_of << unique_csv_content;
    unique_csv_of.close();
    output_path_ = (root_ / "output.csv").string();
  }
  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
  std::string csv_path_;
  std::string unique_csv_path_;
  std::string output_path_;
};

TEST_F(JoinProcessorTest, UniqueKeyTableWorking) {
  v2::PsiConfig config;
  config.mutable_protocol_config()->set_protocol(v2::Protocol::PROTOCOL_ECDH);
  config.mutable_protocol_config()->set_role(v2::Role::ROLE_RECEIVER);
  config.mutable_protocol_config()->set_broadcast_result(true);
  config.mutable_input_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_input_config()->set_path(unique_csv_path_);
  config.mutable_output_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_output_config()->set_path(output_path_);
  config.mutable_input_attr()->set_keys_unique(true);
  config.mutable_keys()->Add("id");

  auto processor = JoinProcessor::Make(config, root_);

  auto key_info = processor->GetUniqueKeysInfo();

  EXPECT_EQ(key_info->DupKeyCnt(), 0);
  EXPECT_EQ(key_info->KeyCnt(), 3);
  EXPECT_EQ(key_info->OriginCnt(), 3);

  // peer 2 + 1 line dup
  MemoryIndexReader index_reader({0}, {2});
  auto stat = processor->DealResultIndex(index_reader);
  EXPECT_EQ(stat.self_intersection_count, 1);
  EXPECT_EQ(stat.peer_intersection_count, 3);
  EXPECT_EQ(stat.original_count, 3);
  EXPECT_EQ(stat.join_intersection_count, 3);
  EXPECT_EQ(stat.inter_unique_cnt, 1);

  processor->GenerateResult(0);
  EXPECT_EQ(GetFileLine(output_path_), 4);
}

TEST_F(JoinProcessorTest, UniqueKeyTableEmptyResultWorking) {
  v2::PsiConfig config;
  config.mutable_protocol_config()->set_protocol(v2::Protocol::PROTOCOL_ECDH);
  config.mutable_protocol_config()->set_role(v2::Role::ROLE_RECEIVER);
  config.mutable_protocol_config()->set_broadcast_result(true);
  config.mutable_input_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_input_config()->set_path(unique_csv_path_);
  config.mutable_output_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_output_config()->set_path(output_path_);
  config.mutable_input_attr()->set_keys_unique(true);
  config.mutable_keys()->Add("id");

  auto processor = JoinProcessor::Make(config, root_);

  auto key_info = processor->GetUniqueKeysInfo();

  EXPECT_EQ(key_info->DupKeyCnt(), 0);
  EXPECT_EQ(key_info->KeyCnt(), 3);
  EXPECT_EQ(key_info->OriginCnt(), 3);

  MemoryIndexReader index_reader({}, {});
  auto stat = processor->DealResultIndex(index_reader);
  EXPECT_EQ(stat.self_intersection_count, 0);
  EXPECT_EQ(stat.peer_intersection_count, 0);
  EXPECT_EQ(stat.original_count, 3);
  EXPECT_EQ(stat.join_intersection_count, 0);
  EXPECT_EQ(stat.inter_unique_cnt, 0);

  processor->GenerateResult(0);
  EXPECT_EQ(GetFileLine(output_path_), 1);
}

TEST_F(JoinProcessorTest, Working) {
  v2::PsiConfig config;
  config.mutable_protocol_config()->set_protocol(v2::Protocol::PROTOCOL_ECDH);
  config.mutable_protocol_config()->set_role(v2::Role::ROLE_RECEIVER);
  config.mutable_protocol_config()->set_broadcast_result(true);
  config.mutable_input_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_input_config()->set_path(csv_path_);
  config.mutable_output_config()->set_type(v2::IO_TYPE_FILE_CSV);
  config.mutable_output_config()->set_path(output_path_);
  config.mutable_keys()->Add("id");

  auto processor = JoinProcessor::Make(config, root_);

  auto key_info = processor->GetUniqueKeysInfo();

  EXPECT_EQ(key_info->DupKeyCnt(), 1);
  EXPECT_EQ(key_info->KeyCnt(), 3);
  EXPECT_EQ(key_info->OriginCnt(), 4);

  MemoryIndexReader index_reader({0}, {0});
  auto stat = processor->DealResultIndex(index_reader);
  EXPECT_EQ(stat.self_intersection_count, 2);
  EXPECT_EQ(stat.peer_intersection_count, 1);
  EXPECT_EQ(stat.original_count, 4);
  EXPECT_EQ(stat.join_intersection_count, 2);
  EXPECT_EQ(stat.inter_unique_cnt, 1);

  processor->GenerateResult(0);
  EXPECT_EQ(GetFileLine(output_path_), 3);
}

}  // namespace psi
