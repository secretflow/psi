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

#include "psi/wrapper/apsi/utils/csv_reader.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include "psi/utils/multiplex_disk_cache.h"

// STD
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "arrow/array.h"
#include "arrow/datum.h"
#include "spdlog/spdlog.h"

#include "psi/utils/io.h"
// APSI
#include "apsi/log.h"

#include "psi/wrapper/apsi/utils/common.h"

using namespace std;
using namespace apsi;
using namespace apsi::util;

namespace psi::apsi_wrapper {

std::vector<std::string> GetCsvColumnNames(const std::string& filename) {
  std::ifstream csv_file(filename);
  std::string line;
  YACL_ENFORCE(std::getline(csv_file, line), "Empty file.");

  static const std::vector<std::string> valid_header = {
      "key,value",
      R"("key","value")",
      "key",
      R"("key")",
  };
  auto iter = std::find(valid_header.begin(), valid_header.end(), line);
  YACL_ENFORCE(iter != valid_header.end(),
               "file {} has invalid header {} should be one of {}.", filename,
               line, fmt::join(valid_header, ";"));

  std::vector<std::string> column_names;

  column_names.emplace_back("key");
  if (iter - valid_header.begin() < 2) {
    column_names.emplace_back("value");
  }
  SPDLOG_INFO("read file {} with header {}, column_names: {}", filename, line,
              fmt::join(column_names, ","));
  return column_names;
}

ApsiCsvReader::ApsiCsvReader(const string& file_name) : file_name_(file_name) {
  throw_if_file_invalid(file_name_);

  std::vector<std::string> column_names = GetCsvColumnNames(file_name_);

  for (auto& col : column_names) {
    column_types_[col] = arrow::utf8();
  }

  reader_ = MakeArrowCsvReader(file_name_, column_names);
}

std::shared_ptr<arrow::Schema> ApsiCsvReader::schema() const {
  return reader_->schema();
}

auto ApsiCsvReader::read() -> pair<DBData, vector<string>> {
  int row_cnt = 0;

  DBData result;
  vector<string> orig_items;
  std::shared_ptr<arrow::RecordBatch> batch;

  bool result_type_decided = false;

  while (true) {
    // Attempt to read the first RecordBatch
    arrow::Status status = reader_->ReadNext(&batch);

    if (!status.ok()) {
      APSI_LOG_ERROR("Read csv error.");
    }

    if (batch == nullptr) {
      // Handle end of file
      break;
    }

    arrays_.clear();

    for (int i = 0; i < min(2, batch->num_columns()); i++) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch->column(i)));
    }

    row_cnt += batch->num_rows();

    if (!result_type_decided) {
      result_type_decided = true;

      if (batch->num_columns() >= 2) {
        result = LabeledData{};
      } else {
        result = UnlabeledData{};
      }
    }

    for (int i = 0; i < batch->num_rows(); i++) {
      orig_items.emplace_back(arrays_[0]->Value(i));

      if (holds_alternative<UnlabeledData>(result)) {
        get<UnlabeledData>(result).emplace_back(
            std::string(arrays_[0]->Value(i)));
      } else if (holds_alternative<LabeledData>(result)) {
        Label label;
        label.reserve(arrays_[1]->Value(i).size());
        copy(arrays_[1]->Value(i).begin(), arrays_[1]->Value(i).end(),
             back_inserter(label));

        get<LabeledData>(result).emplace_back(std::string(arrays_[0]->Value(i)),
                                              label);
      } else {
        // Something is terribly wrong
        APSI_LOG_ERROR("Critical error reading data");
        throw runtime_error("variant is in bad state");
      }
    }
  }

  YACL_ENFORCE(row_cnt != 0, "empty file : {}", file_name_);
  YACL_ENFORCE(orig_items.size() == std::unordered_set<std::string>(
                                        orig_items.begin(), orig_items.end())
                                        .size(),
               "source file {} has duplicated keys", file_name_);
  SPDLOG_INFO("Read csv file {}, row cnt is {}", file_name_, row_cnt);

  return {std::move(result), std::move(orig_items)};
}

void ApsiCsvReader::bucketize(size_t bucket_cnt,
                              const std::string& bucket_folder) {
  if (!std::filesystem::exists(bucket_folder)) {
    SPDLOG_INFO("create bucket folder {}", bucket_folder);
    std::filesystem::create_directories(bucket_folder);
  }

  MultiplexDiskCache disk_cache(bucket_folder, false);

  std::vector<std::unique_ptr<io::OutputStream>> bucket_os_vec;
  disk_cache.CreateOutputStreams(bucket_cnt, &bucket_os_vec);
  for (auto& out : bucket_os_vec) {
    if (reader_->schema()->num_fields() == 1) {
      out->Write("key\n");

    } else {
      out->Write("key,value\n");
    }
  }

  std::shared_ptr<arrow::RecordBatch> batch;

  while (true) {
    // Attempt to read the first RecordBatch
    arrow::Status status = reader_->ReadNext(&batch);

    if (!status.ok()) {
      APSI_LOG_ERROR("Read csv error.");
    }

    if (batch == nullptr) {
      // Handle end of file
      break;
    }

    arrays_.clear();
    if (batch->num_columns() > 2) {
      SPDLOG_WARN(
          "col cnt of csv file {} is greater than 2, so extra cols are "
          "ignored.",
          file_name_);
    }

    for (int i = 0; i < min(2, batch->num_columns()); i++) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch->column(i)));
    }

    for (int i = 0; i < batch->num_rows(); i++) {
      std::string item(arrays_[0]->Value(i));
      int bucket_idx = std::hash<std::string>()(item) % bucket_cnt;
      auto& out = bucket_os_vec[bucket_idx];

      if (batch->num_columns() == 1) {
        out->Write(fmt::format("\"{}\"\n", item));
      } else {
        out->Write(fmt::format("\"{}\",\"{}\"\n", item, arrays_[1]->Value(i)));
      }
    }
  }

  for (const auto& out : bucket_os_vec) {
    out->Flush();
  }

  bucket_os_vec.clear();
}

void ApsiCsvReader::GroupBucketize(size_t bucket_cnt,
                                   const std::string& bucket_folder,
                                   size_t group_cnt,
                                   MultiplexDiskCache& disk_cache) {
  if (!std::filesystem::exists(bucket_folder)) {
    SPDLOG_INFO("create bucket folder {}", bucket_folder);
    std::filesystem::create_directories(bucket_folder);
  }

  std::vector<std::unique_ptr<io::OutputStream>> bucket_group_vec;
  disk_cache.CreateOutputStreams(group_cnt, &bucket_group_vec);
  for (auto& out : bucket_group_vec) {
    if (reader_->schema()->num_fields() == 1) {
      out->Write("bucket_id,key\n");

    } else if (reader_->schema()->num_fields() == 2) {
      out->Write("bucket_id,key,value\n");
    } else {
      YACL_THROW(
          "col cnt {} of csv file {} is not 1 or 2, so extra cols are "
          "ignored.",
          reader_->schema()->num_fields(), file_name_);
    }
  }

  bool empty = true;
  std::shared_ptr<arrow::RecordBatch> batch;
  auto per_group_bucket = (bucket_cnt + group_cnt - 1) / group_cnt;
  SPDLOG_INFO("{} group, {} bucket, per_group{}", group_cnt, bucket_cnt,
              per_group_bucket);

  while (true) {
    // Attempt to read the first RecordBatch
    arrow::Status status = reader_->ReadNext(&batch);

    YACL_ENFORCE(status.ok(), "Read csv error.");

    if (batch == nullptr) {
      // Handle end of file
      break;
    }

    SPDLOG_INFO("process {} lines", batch->num_rows());

    arrays_.clear();
    if (batch->num_columns() > 2) {
      SPDLOG_WARN(
          "col cnt of csv file {} is greater than 2, so extra cols are "
          "ignored.",
          file_name_);
    }

    for (int i = 0; i < min(2, batch->num_columns()); i++) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch->column(i)));
    }

    for (int i = 0; i < batch->num_rows(); i++) {
      empty = false;
      std::string item(arrays_[0]->Value(i));
      int bucket_idx = std::hash<std::string>()(item) % bucket_cnt;
      int group_idx = bucket_idx / per_group_bucket;
      auto& out = bucket_group_vec[group_idx];

      if (batch->num_columns() == 1) {
        out->Write(fmt::format("{},\"{}\"\n", bucket_idx, item));
      } else {
        out->Write(fmt::format("{},\"{}\",\"{}\"\n", bucket_idx, item,
                               arrays_[1]->Value(i)));
      }
    }
  }

  YACL_ENFORCE(!empty, "empty file : {}", file_name_);

  for (const auto& out : bucket_group_vec) {
    out->Flush();
  }

  bucket_group_vec.clear();
}

}  // namespace psi::apsi_wrapper
