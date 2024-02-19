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

#include "psi/utils/csv_header_parser.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#include "gtest/gtest.h"

namespace psi {

constexpr auto csv_content = R"csv(id1,id2,y1
1,"b","y1_1"
1,"b","y1_2"
3,"c","y1_3"
4,"b","y1_4"
2,"a","y1_5"
2,"a","y1_6"
)csv";

TEST(CsvHeaderParserTest, Works) {
  std::filesystem::path csv_path =
      std::filesystem::temp_directory_path() / "csv_header_parser_test.csv";

  {
    std::ofstream file;
    file.open(csv_path);
    file << csv_content;
    file.close();
  }

  CsvHeaderParser parser(csv_path);
  EXPECT_EQ(parser.target_indices(std::vector<std::string>{"id2", "id1", "y1"}),
            (std::vector<size_t>{1, 0, 2}));

  EXPECT_EQ(parser.target_indices(
                std::vector<std::string>{"y1", "id2", "id2", "id1"}, 1),
            (std::vector<size_t>{3, 2, 2, 1}));

  { std::filesystem::remove(csv_path); }
}

}  // namespace psi
