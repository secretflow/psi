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

#pragma once

#include <string>
#include <vector>

#include "arrow/api.h"
#include "arrow/csv/api.h"

namespace psi {

// The delimiter used to merge rows and columns
constexpr char kColumnDelimiter = 0x1E;
constexpr char kRowDelimiter = 0x1F;

// This class provides functionalities to convert CSV files which is used to
// support duplicate-key PIR queries
class ApsiCsvConverter {
 public:
  explicit ApsiCsvConverter(const std::string& input_file_path,
                            const std::string& key,
                            const std::vector<std::string>& labels = {});

  // This method helps the sender to convert the db file. Specifically, it
  // first splices multiple labels by column and then joins different rows
  // together based on the same key. At the same time, the column names of the
  // file are adjusted to "key,value" to fit APSI. In addition, it can count
  // the number of rows for each key, which will be used in the second stage.
  void MergeColumnAndRow(const std::string& key_value_file_path,
                         const std::string& key_count_file_path = "");

  // This method helps the receiver to extract the query from the original query
  // file. The column name will be changed to “key”. This function does not
  // implement duplicate key checking, which will be done in APSI.
  void ExtractQueryTo(const std::string& query_file_path);

  // This method helps the receiver to extract the result from the output of
  // APSI. Concretely, it splits the file by row and column to get all the
  // labels. The key_name and label_names you provide will be used as column
  // names in the result file.
  int ExtractResult(const std::string& result_file_path,
                    const std::string& key_name,
                    const std::vector<std::string>& label_names);

 private:
  std::string input_file_path_;

  // Key column name of the input file
  std::string key_;

  // Label column names of the input file
  std::vector<std::string> labels_;

  // Apache Arrow CSV reader for streaming reading
  std::shared_ptr<arrow::csv::StreamingReader> reader_;

  // Apache Arrow record batch
  std::shared_ptr<arrow::RecordBatch> batch_;

  // Apache Arrow string arrays for storing column data
  std::vector<std::shared_ptr<arrow::StringArray>> arrays_;

  // The delimiter used to merge rows and columns
  std::string column_delimiter_;
  std::string row_delimiter_;

};  // class ApsiCsvConverter
}  // namespace psi