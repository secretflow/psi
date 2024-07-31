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

#include "psi/apsi_wrapper/utils/csv_reader.h"

#include "psi/utils/multiplex_disk_cache.h"

// STD
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

#include "arrow/array.h"
#include "arrow/datum.h"
#include "spdlog/spdlog.h"

#include "psi/utils/io.h"
// APSI
#include "apsi/log.h"

#include "psi/apsi_wrapper/utils/common.h"

using namespace std;
using namespace apsi;
using namespace apsi::util;

namespace psi::apsi_wrapper {

CSVReader::CSVReader(const string& file_name) : file_name_(file_name) {
  throw_if_file_invalid(file_name_);

  std::ifstream csv_file(file_name_);
  std::string line;
  if (!std::getline(csv_file, line)) {
    APSI_LOG_WARNING("Nothing to read in `" << file_name_ << "`");
    empty_file_ = true;
    return;
  }

  infile_ =
      arrow::io::ReadableFile::Open(file_name_, arrow::default_memory_pool())
          .ValueOrDie();

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  read_options.autogenerate_column_names = true;

  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  reader_ = arrow::csv::StreamingReader::Make(io_context, infile_, read_options,
                                              parse_options, convert_options)
                .ValueOrDie();
}

auto CSVReader::read() -> pair<DBData, vector<string>> {
  int row_cnt = 0;

  DBData result;
  vector<string> orig_items;

  bool result_type_decided = false;

  if (empty_file_) {
    APSI_LOG_WARNING("Nothing to read in `" << file_name_ << "`");
    return {UnlabeledData{}, {}};
  }

  while (true) {
    // Attempt to read the first RecordBatch
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      APSI_LOG_ERROR("Read csv error.");
    }

    if (batch_ == nullptr) {
      // Handle end of file
      break;
    }

    arrays_.clear();
    if (batch_->num_columns() > 2) {
      SPDLOG_WARN(
          "col cnt of csv file {} is greater than 2, so extra cols are "
          "ignored.",
          file_name_);
    }

    for (int i = 0; i < min(2, batch_->num_columns()); i++) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(i)));
    }

    row_cnt += batch_->num_rows();

    if (!result_type_decided) {
      result_type_decided = true;

      if (batch_->num_columns() >= 2) {
        result = LabeledData{};
      } else {
        result = UnlabeledData{};
      }
    }

    for (int i = 0; i < batch_->num_rows(); i++) {
      orig_items.push_back(std::string(arrays_[0]->Value(i)));

      if (holds_alternative<UnlabeledData>(result)) {
        get<UnlabeledData>(result).push_back(std::string(arrays_[0]->Value(i)));
      } else if (holds_alternative<LabeledData>(result)) {
        Label label;
        label.reserve(arrays_[1]->Value(i).size());
        copy(arrays_[1]->Value(i).begin(), arrays_[1]->Value(i).end(),
             back_inserter(label));

        get<LabeledData>(result).push_back(
            make_pair(std::string(arrays_[0]->Value(i)), label));
      } else {
        // Something is terribly wrong
        APSI_LOG_ERROR("Critical error reading data");
        throw runtime_error("variant is in bad state");
      }
    }
  }

  if (row_cnt == 0) {
    APSI_LOG_WARNING("Nothing to read in `" << file_name_ << "`");
    return {UnlabeledData{}, {}};
  } else {
    SPDLOG_INFO("Read csv file {}, row cnt is {}", file_name_, row_cnt);
    return {std::move(result), std::move(orig_items)};
  }
}

void CSVReader::bucketize(size_t bucket_cnt, const std::string& bucket_folder) {
  throw_if_directory_invalid(bucket_folder);

  MultiplexDiskCache disk_cache(bucket_folder, false);

  std::vector<std::unique_ptr<io::OutputStream>> bucket_os_vec;
  disk_cache.CreateOutputStreams(bucket_cnt, &bucket_os_vec);

  if (empty_file_) {
    for (const auto& out : bucket_os_vec) {
      out->Flush();
    }

    bucket_os_vec.clear();

    return;
  }

  int row_cnt = 0;

  while (true) {
    // Attempt to read the first RecordBatch
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      APSI_LOG_ERROR("Read csv error.");
    }

    if (batch_ == nullptr) {
      // Handle end of file
      break;
    }

    arrays_.clear();
    if (batch_->num_columns() > 2) {
      SPDLOG_WARN(
          "col cnt of csv file {} is greater than 2, so extra cols are "
          "ignored.",
          file_name_);
    }

    for (int i = 0; i < min(2, batch_->num_columns()); i++) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(i)));
    }

    row_cnt += batch_->num_rows();

    for (int i = 0; i < batch_->num_rows(); i++) {
      std::string item(arrays_[0]->Value(i));
      int bucket_idx = std::hash<std::string>()(item) % bucket_cnt;
      auto& out = bucket_os_vec[bucket_idx];

      if (batch_->num_columns() == 1) {
        out->Write(fmt::format("\"{}\"\n", item));
      } else {
        out->Write(fmt::format("\"{}\",\"{}\"\n", item, arrays_[1]->Value(i)));
      }
    }
  }

  (void)row_cnt;

  for (const auto& out : bucket_os_vec) {
    out->Flush();
  }

  bucket_os_vec.clear();
}

}  // namespace psi::apsi_wrapper
