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

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

// STD
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// APSI
#include "apsi/item.h"
#include "apsi/psi_params.h"
#include "apsi/util/db_encoding.h"

// arrow
#include "arrow/csv/api.h"
#include "arrow/io/api.h"

#include "psi/apsi_wrapper/utils/common.h"

namespace psi::apsi_wrapper {

/**
Simple CSV file parser
*/
class CSVReader {
 public:
  explicit CSVReader(const std::string& file_name);

  std::pair<DBData, std::vector<std::string>> read();

  void bucketize(size_t bucket_cnt, const std::string& bucket_folder);

 private:
  std::string file_name_;

  std::shared_ptr<arrow::io::ReadableFile> infile_;

  std::shared_ptr<arrow::csv::StreamingReader> reader_;

  std::shared_ptr<arrow::RecordBatch> batch_;

  std::vector<std::shared_ptr<arrow::StringArray>> arrays_;

  bool empty_file_ = false;
};  // class CSVReader

}  // namespace psi::apsi_wrapper
