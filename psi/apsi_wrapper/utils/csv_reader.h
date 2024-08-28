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
#include "psi/utils/multiplex_disk_cache.h"

namespace psi::apsi_wrapper {

/**
Simple CSV file parser
*/
class ApsiCsvReader {
 public:
  explicit ApsiCsvReader(const std::string& file_name);

  std::pair<DBData, std::vector<std::string>> read();

  void bucketize(size_t bucket_cnt, const std::string& bucket_folder);

  void GroupBucketize(size_t bucket_cnt, const std::string& bucket_folder,
                      size_t group_cnt, MultiplexDiskCache& disk_cache);

  std::shared_ptr<arrow::Schema> schema() const;

 private:
  std::string file_name_;

  std::shared_ptr<arrow::csv::StreamingReader> reader_;

  std::vector<std::shared_ptr<arrow::StringArray>> arrays_;

  std::unordered_map<std::string, std::shared_ptr<arrow::DataType>>
      column_types_;
};  // class ApsiCsvReader

}  // namespace psi::apsi_wrapper
