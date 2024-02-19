// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/utils/index_store.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numeric>

#include "gtest/gtest.h"

namespace psi {

class IndexStoreTest : public ::testing::Test {
 protected:
  void TearDown() override { std::filesystem::remove(index_store_path_); }

  std::filesystem::path index_store_path_ =
      std::filesystem::temp_directory_path() / "index_store_test.csv";
};

TEST_F(IndexStoreTest, Works) {
  {
    IndexWriter writer(index_store_path_);

    for (uint64_t i = 0; i < 5000; i++) {
      EXPECT_EQ(writer.WriteCache(i), i + 1);
    }

    EXPECT_EQ(writer.cache_cnt(), 5000);
    EXPECT_EQ(writer.write_cnt(), 5000);
  }
  {
    IndexWriter writer(index_store_path_);

    std::vector<uint64_t> indexes;
    indexes.resize(5000);

    std::iota(indexes.begin(), indexes.end(), 5000);

    EXPECT_EQ(writer.WriteCache(indexes), 5000);

    EXPECT_EQ(writer.cache_cnt(), 5000);
    EXPECT_EQ(writer.write_cnt(), 5000);

    writer.Commit();

    EXPECT_EQ(writer.cache_cnt(), 0);
    EXPECT_EQ(writer.write_cnt(), 5000);
  }
  {
    IndexWriter writer(index_store_path_);

    for (uint64_t i = 10000; i < 15000; i++) {
      writer.WriteCache(i);

      EXPECT_EQ(writer.cache_cnt(), 1);
      EXPECT_EQ(writer.write_cnt(), i - 9999);

      writer.Commit();

      EXPECT_EQ(writer.cache_cnt(), 0);
      EXPECT_EQ(writer.write_cnt(), i - 9999);
    }
  }
  {
    IndexWriter writer(index_store_path_);

    std::vector<uint64_t> indexes;
    indexes.resize(5000);

    std::iota(indexes.begin(), indexes.end(), 15000);

    writer.WriteCache(indexes);

    EXPECT_EQ(writer.cache_cnt(), 5000);

    writer.Commit();

    EXPECT_EQ(writer.cache_cnt(), 0);
  }

  {
    IndexReader reader(index_store_path_);

    uint64_t idx = 0;

    while (reader.HasNext()) {
      EXPECT_EQ(reader.GetNext().value(), idx);
      idx++;
      EXPECT_EQ(reader.read_cnt(), idx);
    }

    EXPECT_EQ(idx, 20000);
    EXPECT_EQ(reader.read_cnt(), 20000);

    EXPECT_FALSE(reader.HasNext());
    EXPECT_FALSE(reader.GetNext().has_value());

    EXPECT_FALSE(reader.HasNext());
    EXPECT_FALSE(reader.GetNext().has_value());
  }
}

TEST_F(IndexStoreTest, Empty) {
  { IndexWriter writer(index_store_path_); }

  {
    IndexReader reader(index_store_path_);
    EXPECT_FALSE(reader.HasNext());
    EXPECT_FALSE(reader.HasNext());
    EXPECT_FALSE(reader.GetNext().has_value());
    EXPECT_FALSE(reader.HasNext());
  }
}

}  // namespace psi
