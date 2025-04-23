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

#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "arrow/api.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/wrapper/apsi/utils/common.h"

namespace psi {

void WriteCsvFile(
    const std::string& file_path,
    const std::unordered_map<std::string, std::pair<std::string, int>>&
        key_value_map,
    bool is_count) {
  std::ofstream writer(file_path);

  if (!writer.is_open()) {
    YACL_THROW("Open file {} failed.", file_path);
  }

  writer << "key,value\n";
  for (const auto& [key, value_count_pair] : key_value_map) {
    if (is_count) {
      writer << key << "," << value_count_pair.second << "\n";
    } else {
      writer << key << "," << value_count_pair.first << "\n";
    }
  }

  writer.close();
}

std::shared_ptr<arrow::csv::StreamingReader> MakeStreamingReader(
    const std::string& file_path,
    const std::vector<std::string>& column_names) {
  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile =
      arrow::io::ReadableFile::Open(file_path, arrow::default_memory_pool())
          .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  for (auto& col : column_names) {
    convert_options.column_types[col] = arrow::utf8();
  }

  convert_options.include_columns.insert(convert_options.include_columns.end(),
                                         column_names.begin(),
                                         column_names.end());

  return arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                           parse_options, convert_options)
      .ValueOrDie();
}

ApsiCsvConverter::ApsiCsvConverter(const std::string& input_file_path,
                                   const std::string& key,
                                   const std::vector<std::string>& labels)
    : input_file_path_(input_file_path),
      key_(key),
      labels_(labels),
      column_delimiter_(std::string(1, kColumnDelimiter)),
      row_delimiter_(std::string(1, kRowDelimiter)) {
  psi::apsi_wrapper::throw_if_file_invalid(input_file_path_);

  std::ifstream csv_file(input_file_path_);
  std::string first_line, second_line;

  YACL_ENFORCE(std::getline(csv_file, first_line), "Empty file: {}.",
               input_file_path_);
  YACL_ENFORCE(std::getline(csv_file, second_line),
               "File {} only has one line with column names.",
               input_file_path_);

  std::vector<std::string> column_names;
  column_names.emplace_back(key_);
  column_names.insert(column_names.end(), labels_.begin(), labels_.end());

  reader_ = MakeStreamingReader(input_file_path_, column_names);
}

void ApsiCsvConverter::MergeColumnAndRow(
    const std::string& key_value_file_path,
    const std::string& key_count_file_path) {
  YACL_ENFORCE(!labels_.empty(), "You must provide labels.");

  // Key -> (Value, Count)
  // Value represents the concatenated labels.
  // Count represents the number of rows with the same key.
  std::unordered_map<std::string, std::pair<std::string, int>> key_value_map;

  while (true) {
    // Attempt to read the first RecordBatch.
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (!batch_) {
      // Reach the end of csv file.
      break;
    }

    arrays_.clear();
    for (int i = 0; i < batch_->num_columns(); ++i) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(i)));
    }

    // Merge multiple labels within a row using 0x1E, and for the same key,
    // concatenate the values from multiple rows using 0x1F.
    for (int i = 0; i < batch_->num_rows(); ++i) {
      std::string current_key = (std::string)arrays_[0]->Value(i);

      std::vector<absl::string_view> labels;
      labels.reserve(arrays_.size() - 1);
      for (size_t j = 1; j < arrays_.size(); ++j) {
        labels.emplace_back(arrays_[j]->Value(i));
      }

      std::string current_value = absl::StrJoin(labels, column_delimiter_);

      if (key_value_map.find(current_key) != key_value_map.end()) {
        key_value_map[current_key].first += row_delimiter_ + current_value;
        key_value_map[current_key].second++;
      } else {
        key_value_map[current_key] = std::make_pair(current_value, 1);
      }
    }
  }

  WriteCsvFile(key_value_file_path, key_value_map, false);
  SPDLOG_INFO("Write key-value file: {}", key_value_file_path);

  if (!key_count_file_path.empty()) {
    WriteCsvFile(key_count_file_path, key_value_map, true);
    SPDLOG_INFO("Write key-count file: {}", key_count_file_path);
  }
}

void ApsiCsvConverter::ExtractQueryTo(const std::string& query_file_path) {
  std::vector<std::string> queries;

  while (true) {
    // Attempt to read the first RecordBatch.
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (!batch_) {
      // Reach the end of csv file.
      break;
    }

    YACL_ENFORCE(batch_->num_columns() == 1,
                 "The input file should read only one column.");

    std::shared_ptr<arrow::StringArray> key_array =
        std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(0));

    for (int i = 0; i < batch_->num_rows(); ++i) {
      queries.emplace_back(key_array->Value(i));
    }
  }

  std::ofstream writer(query_file_path);

  if (!writer.is_open()) {
    YACL_THROW("Open file {} failed.", query_file_path);
  }

  writer << "key\n";
  for (const auto& query : queries) {
    writer << query << "\n";
  }

  writer.close();
  SPDLOG_INFO("Write query file: {}", query_file_path);
}

int ApsiCsvConverter::ExtractResult(
    const std::string& result_file_path, const std::string& key_name,
    const std::vector<std::string>& label_names) {
  YACL_ENFORCE(!label_names.empty(), "You must provide label names.");

  // Count the total number of rows of the query result
  int total_row_cnt = 0;

  std::stringstream csv_output;
  csv_output << key_name;
  for (const auto& name : label_names) {
    csv_output << "," << name;
  }
  csv_output << "\n";

  while (true) {
    // Attempt to read the first RecordBatch.
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (!batch_) {
      // Reach the end of csv file.
      break;
    }

    YACL_ENFORCE(batch_->num_columns() == 2,
                 "The input file should read only two columns.");

    arrays_.clear();
    for (int i = 0; i < batch_->num_columns(); ++i) {
      arrays_.emplace_back(
          std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(i)));
    }

    for (int i = 0; i < batch_->num_rows(); ++i) {
      std::string current_key = (std::string)arrays_[0]->Value(i);
      std::string current_value = (std::string)arrays_[1]->Value(i);

      // First, split the value by row based on the separator 0x1F
      std::vector<std::string> row_values =
          absl::StrSplit(current_value, row_delimiter_);
      total_row_cnt += row_values.size();

      // Second, split the row_value by column based on the separator 0x1E
      for (auto& row_value : row_values) {
        std::vector<std::string> current_labels =
            absl::StrSplit(row_value, column_delimiter_);

        YACL_ENFORCE(
            label_names.size() == current_labels.size(),
            "Incorrect value:{}, which cannot be split into {} labels.",
            row_value, label_names.size());

        csv_output << current_key;
        for (auto& label : current_labels) {
          csv_output << "," << label;
        }
        csv_output << "\n";
      }
    }
  }

  std::ofstream writer(result_file_path);
  writer << csv_output.str();
  writer.close();
  SPDLOG_INFO("Write result file: {}", result_file_path);

  return total_row_cnt;
}

}  // namespace psi