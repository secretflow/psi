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

#include "psi/utils/arrow_csv_batch_provider.h"

#include <filesystem>
#include <memory>

#include "gtest/gtest.h"

namespace psi {
namespace {

constexpr auto content = R"csv(id1,id2,id3,label1,label2
1,"one","first","x","X"
2,"two","second","y","Y"
3,"three","third","z","Z"
)csv";

TEST(ArrowCsvBatchProvider, works) {
  std::filesystem::path file_path = std::filesystem::temp_directory_path() /
                                    "arrow_csv_batch_provider_test.csv";

  std::ofstream file;
  file.open(file_path);
  file << content;
  file.close();

  {
    ArrowCsvBatchProvider provider(file_path, {"id1", "id2", "id3"}, 1);
    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"1,one,first"}));
    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"2,two,second"}));
    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"3,three,third"}));
    EXPECT_EQ(provider.row_cnt(), 3);
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_EQ(provider.row_cnt(), 3);
  }

  {
    ArrowCsvBatchProvider provider(file_path, {"id2", "id1"}, 3);
    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"one,1", "two,2", "three,3"}));
    EXPECT_EQ(provider.row_cnt(), 3);
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_EQ(provider.row_cnt(), 3);
  }

  {
    ArrowCsvBatchProvider provider(file_path, {"id3"}, 5);
    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"first", "second", "third"}));
    EXPECT_EQ(provider.row_cnt(), 3);
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_EQ(provider.row_cnt(), 3);
  }

  {
    ArrowCsvBatchProvider provider(file_path, {"id1", "id2", "id3"}, 1,
                                   {"label1", "label2"});

    auto key_value_pair = provider.ReadNextLabeledBatch();
    EXPECT_EQ(key_value_pair.first, std::vector<std::string>({"1,one,first"}));
    EXPECT_EQ(key_value_pair.second, std::vector<std::string>({"x,X"}));

    EXPECT_EQ(provider.ReadNextBatch(),
              std::vector<std::string>({"2,two,second"}));

    key_value_pair = provider.ReadNextLabeledBatch();
    EXPECT_EQ(key_value_pair.first,
              std::vector<std::string>({"3,three,third"}));
    EXPECT_EQ(key_value_pair.second, std::vector<std::string>({"z,Z"}));
    EXPECT_EQ(provider.row_cnt(), 3);
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_TRUE(provider.ReadNextBatch().empty());
    EXPECT_EQ(provider.row_cnt(), 3);
  }

  std::error_code ec;
  std::filesystem::remove(file_path, ec);
  EXPECT_EQ(ec.value(), 0);
}

}  // namespace
}  // namespace psi
