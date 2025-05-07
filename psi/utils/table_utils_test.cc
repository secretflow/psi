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

#include "psi/utils/table_utils.h"

#include <ctime>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

#include "psi/utils/index_store.h"

namespace psi {

class TableUtilTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_dir_ = std::filesystem::temp_directory_path() / "table_utils_test";
    if (!std::filesystem::exists(root_dir_)) {
      std::filesystem::create_directories(root_dir_);
    }

    csv_path_ = root_dir_ / "table_util_test.csv";
    unique_key_csv_path_ = root_dir_ / "unique_table_util_test.csv";
    sorted_csv_path_ = root_dir_ / "table_util_test.csv.sorted";
    intersect_path_ = root_dir_ / "table_util_test.csv.sorted.inter";
    except_path_ = root_dir_ / "table_util_test.csv.sorted.except";
    key_info_path_ = root_dir_ / "table_util_test.csv.sorted.keyinfo";
    index_path_ = root_dir_ / "index_test.csv";

    std::ofstream ofstream(csv_path_.string());
    ofstream << "id,id2,name\n"
             << "1,1,alice\n"
             << "2,2,bob\n"
             << "3,3,davy\n"
             << "2,2,carol\n"
             << "4,4,ear\n"
             << "4,4,ear\n";
    ofstream.close();
    std::ofstream ofstream_unique_table(unique_key_csv_path_.string());
    ofstream_unique_table << "id,id2,name\n"
                          << "1,1,alice\n"
                          << "2,2,bob\n"
                          << "3,3,davy\n"
                          << "4,4,ear\n";
    ofstream_unique_table.flush();
    ofstream_unique_table.close();

    IndexWriter writer(index_path_, 10000, true);
    writer.WriteCache({1, 2}, {2, 2});
  }
  void TearDown() override { std::filesystem::remove_all(root_dir_); }

  std::filesystem::path root_dir_;

  std::filesystem::path csv_path_;
  std::filesystem::path unique_key_csv_path_;
  std::filesystem::path sorted_csv_path_;
  std::filesystem::path index_path_;
  std::filesystem::path key_info_path_;
  std::filesystem::path intersect_path_;
  std::filesystem::path except_path_;
};

TEST_F(TableUtilTest, TableToCsv) {
  auto table = Table::MakeFromCsv(csv_path_.string());

  auto sort_table =
      SortedTable::Make(table, sorted_csv_path_.string(), {"id", "id2"});

  auto key_info = KeyInfo::Make(sort_table, key_info_path_.string());
  EXPECT_EQ(key_info->DupKeyCnt(), 2);

  auto provider = key_info->GetKeysProviderWithDupCnt();
  auto batch = provider->ReadNextBatchWithDupCnt();
  EXPECT_EQ(batch.first.size(), 4);
  EXPECT_EQ(batch.second.size(), 2);
  EXPECT_EQ(batch.first[0], "1,1");
  EXPECT_EQ(batch.first[1], "2,2");
  EXPECT_EQ(batch.second[1], 1);
  EXPECT_EQ(batch.second[3], 1);

  FileIndexReader reader(index_path_);
  ResultDumper dumper(intersect_path_.string(), except_path_.string());
  auto stat = key_info->ApplyPeerDupCnt(reader, dumper);

  EXPECT_EQ(stat.self_intersection_count, 3);
  EXPECT_EQ(stat.peer_intersection_count, 6);
  EXPECT_EQ(stat.original_count, 6);
  EXPECT_EQ(stat.join_intersection_count, 9);
}

TEST_F(TableUtilTest, UniqueTableToCsv) {
  auto unique_table =
      UniqueKeyTable::Make(unique_key_csv_path_.string(), "csv", {"id", "id2"});

  auto key_info = KeyInfo::Make(unique_table);

  EXPECT_EQ(key_info->DupKeyCnt(), 0);

  auto provider = key_info->GetKeysProviderWithDupCnt();
  auto batch = provider->ReadNextBatchWithDupCnt();
  EXPECT_EQ(batch.first.size(), 4);
  EXPECT_EQ(batch.second.size(), 0);
  EXPECT_EQ(batch.first[0], "1,1");
  EXPECT_EQ(batch.first[1], "2,2");

  FileIndexReader reader(index_path_);
  ResultDumper dumper(intersect_path_.string(), except_path_.string());
  auto stat = key_info->ApplyPeerDupCnt(reader, dumper);

  EXPECT_EQ(stat.self_intersection_count, 2);
  EXPECT_EQ(stat.peer_intersection_count, 6);
  EXPECT_EQ(stat.original_count, 4);
  EXPECT_EQ(stat.join_intersection_count, 6);
}

}  // namespace psi
