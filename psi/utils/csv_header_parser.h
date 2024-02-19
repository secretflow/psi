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

#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace psi {

// Just another version of CsvHeaderAnalyzer based on Apache Arrow.
class CsvHeaderParser {
 public:
  explicit CsvHeaderParser(const std::string& path);

  // Return interested fields indices.
  [[nodiscard]] std::vector<size_t> target_indices(
      const std::vector<std::string>& target_fields, size_t offset = 0) const;

 private:
  std::string path_;

  std::unordered_map<std::string, size_t> key_index_map_;
};

}  // namespace psi
