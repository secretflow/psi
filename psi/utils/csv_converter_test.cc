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

#include "psi/utils/csv_converter.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/random_str.h"

namespace psi {

namespace {

constexpr auto csv_content = R"csv(id,label1,label2,label3,label4
1,"b","y1",0.12,"one"
3,"c","y3",0.9,"three"
4,"b","y4",-12,"four"
1,"b","y1",-0.13,"two"
)csv";

std::unordered_set<std::string> ReadCsvRow(const std::string& file_path) {
  std::unordered_set<std::string> lines;
  std::ifstream file(file_path);
  std::string line;
  while (std::getline(file, line)) {
    lines.insert(line);
  }
  return lines;
}

TEST(ApsiCsvConverterTest, Works) {
  auto uuid_str = GetRandomString();

  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};

  std::filesystem::create_directories(tmp_folder);

  std::string input_path = tmp_folder / "csv_converter_input.csv";

  std::string key_value_file_path = tmp_folder / "csv_converter_key_value.csv";

  std::string key_count_file_path = tmp_folder / "csv_converter_key_count.csv";

  std::string query_file_path = tmp_folder / "csv_converter_query.csv";

  std::string result_file_path = tmp_folder / "csv_converter_result.csv";

  std::ofstream file(input_path);
  file << csv_content;
  file.close();

  std::vector<std::string> read_keys;
  std::vector<std::string> read_values;

  // Only one label, check if the row merging is valid.
  {
    ApsiCsvConverter converter(input_path, "id", {"label4"});
    converter.MergeColumnAndRow(key_value_file_path);
    ArrowCsvBatchProvider provider(key_value_file_path, {"key"}, 5, {"value"});
    tie(read_keys, read_values) = provider.ReadNextLabeledBatch();

    EXPECT_EQ(read_keys.size(), 3);
    EXPECT_EQ(read_values.size(), 3);
    for (size_t i = 0; i < read_keys.size(); ++i) {
      if (read_keys[i] == "1") {
        EXPECT_EQ(read_values[i], "one||two");
      } else if (read_keys[i] == "3") {
        EXPECT_EQ(read_values[i], "three");
      } else {
        EXPECT_EQ(read_values[i], "four");
      }
    }

    ApsiCsvConverter converter1(key_value_file_path, "key", {"value"});
    int cnt = converter1.ExtractResult(result_file_path, "id", {"label4"});

    std::unordered_set<std::string> target_data = {
        "id,label4", "1,one", "3,three", "4,four", "1,two"};
    std::unordered_set<std::string> result = ReadCsvRow(result_file_path);

    EXPECT_EQ(cnt, 4);
    EXPECT_EQ(result, target_data);
  }

  // Check if the column merging is valid.
  {
    ApsiCsvConverter converter(input_path, "id", {"label1", "label2"});
    converter.MergeColumnAndRow(key_value_file_path);
    ArrowCsvBatchProvider provider(key_value_file_path, {"key"}, 5, {"value"});
    tie(read_keys, read_values) = provider.ReadNextLabeledBatch();

    EXPECT_EQ(read_keys.size(), 3);
    EXPECT_EQ(read_values.size(), 3);
    for (size_t i = 0; i < read_keys.size(); ++i) {
      if (read_keys[i] == "1") {
        // Considering the actual requirements, duplicate values will not be
        // filtered.
        EXPECT_EQ(read_values[i], "b;y1||b;y1");
      } else if (read_keys[i] == "3") {
        EXPECT_EQ(read_values[i], "c;y3");
      } else {
        EXPECT_EQ(read_values[i], "b;y4");
      }
    }

    ApsiCsvConverter converter1(key_value_file_path, "key", {"value"});
    int cnt =
        converter1.ExtractResult(result_file_path, "id", {"label1", "label2"});

    std::unordered_set<std::string> target_data = {"id,label1,label2", "1,b,y1",
                                                   "3,c,y3", "4,b,y4"};
    std::unordered_set<std::string> result = ReadCsvRow(result_file_path);

    EXPECT_EQ(cnt, 4);
    EXPECT_EQ(result, target_data);
  }

  // Check if the row and column merging is valid.
  {
    ApsiCsvConverter converter(input_path, "id",
                               {"label1", "label2", "label3"});
    converter.MergeColumnAndRow(key_value_file_path);
    ArrowCsvBatchProvider provider(key_value_file_path, {"key"}, 5, {"value"});
    tie(read_keys, read_values) = provider.ReadNextLabeledBatch();

    EXPECT_EQ(read_keys.size(), 3);
    EXPECT_EQ(read_values.size(), 3);
    for (size_t i = 0; i < read_keys.size(); ++i) {
      if (read_keys[i] == "1") {
        EXPECT_EQ(read_values[i], "b;y1;0.12||b;y1;-0.13");
      } else if (read_keys[i] == "3") {
        EXPECT_EQ(read_values[i], "c;y3;0.9");
      } else {
        EXPECT_EQ(read_values[i], "b;y4;-12");
      }
    }

    ApsiCsvConverter converter1(key_value_file_path, "key", {"value"});
    int cnt = converter1.ExtractResult(result_file_path, "id",
                                       {"label1", "label2", "label3"});

    std::unordered_set<std::string> target_data = {
        "id,label1,label2,label3", "1,b,y1,0.12", "3,c,y3,0.9", "4,b,y4,-12",
        "1,b,y1,-0.13"};
    std::unordered_set<std::string> result = ReadCsvRow(result_file_path);

    EXPECT_EQ(cnt, 4);
    EXPECT_EQ(result, target_data);
  }

  // Check if the counting function is valid when there are duplicate labels.
  {
    ApsiCsvConverter converter(input_path, "id", {"label1"});
    converter.MergeColumnAndRow(key_value_file_path, key_count_file_path);
    ArrowCsvBatchProvider provider(key_count_file_path, {"key"}, 5, {"value"});
    tie(read_keys, read_values) = provider.ReadNextLabeledBatch();

    EXPECT_EQ(read_keys.size(), 3);

    for (size_t i = 0; i < read_keys.size(); ++i) {
      if (read_keys[i] == "1") {
        EXPECT_EQ(read_values[i], "2");
      } else if (read_keys[i] == "3") {
        EXPECT_EQ(read_values[i], "1");
      } else {
        EXPECT_EQ(read_values[i], "1");
      }
    }
  }

  // Check if the counting function is valid when there are no duplicate labels.
  {
    ApsiCsvConverter converter(input_path, "id", {"label1", "label3"});
    converter.MergeColumnAndRow(key_value_file_path, key_count_file_path);
    ArrowCsvBatchProvider provider(key_count_file_path, {"key"}, 5, {"value"});
    tie(read_keys, read_values) = provider.ReadNextLabeledBatch();

    EXPECT_EQ(read_keys.size(), 3);

    for (size_t i = 0; i < read_keys.size(); ++i) {
      if (read_keys[i] == "1") {
        EXPECT_EQ(read_values[i], "2");
      } else if (read_keys[i] == "3") {
        EXPECT_EQ(read_values[i], "1");
      } else {
        EXPECT_EQ(read_values[i], "1");
      }
    }
  }

  // Check if the function ExtractQuery is valid.
  {
    ApsiCsvConverter converter(input_path, "id");
    converter.ExtractQuery(query_file_path);
    ArrowCsvBatchProvider provider(query_file_path, {"key"}, 5);
    read_keys = provider.ReadNextBatch();
    std::vector<std::string> target_keys = {"1", "3", "4", "1"};

    EXPECT_EQ(read_keys.size(), 4);

    for (size_t i = 0; i < read_keys.size(); ++i) {
      EXPECT_EQ(read_keys[i], target_keys[i]);
    }
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

}  // namespace

}  // namespace psi