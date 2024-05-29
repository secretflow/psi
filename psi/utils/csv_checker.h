// Copyright 2022 Ant Group Co., Ltd.
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

#include <cstdint>
#include <string>
#include <vector>

namespace psi {

// TODO(junfeng): replace CsvChecker with CheckCsv.
class CsvChecker {
 public:
  explicit CsvChecker(const std::string& csv_path,
                      const std::vector<std::string>& schema_names,
                      bool skip_check = false);

  uint32_t data_count() const { return data_count_; }

  std::string hash_digest() const { return hash_digest_; }

 private:
  uint32_t data_count_ = 0;

  std::string hash_digest_;
};

struct CheckCsvReport {
  uint64_t num_rows = 0;

  std::string key_hash_digest;

  bool contains_duplicates = false;

  std::string duplicates_keys_file_path;
};

CheckCsvReport CheckCsv(const std::string& input_file_path,
                        const std::vector<std::string>& keys,
                        bool check_duplicates, bool generate_key_hash_digest);

}  // namespace psi
