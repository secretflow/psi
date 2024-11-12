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

#include "psi/utils/arrow_helper.h"

#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>

#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "yacl/base/exception.h"

namespace psi {

std::shared_ptr<arrow::csv::StreamingReader> MakeCsvReader(
    const std::string& path, const std::vector<std::string>& choosed_columns) {
  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  PSI_ARROW_GET_RESULT(infile, arrow::io::ReadableFile::Open(
                                   path, arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  if (!choosed_columns.empty()) {
    convert_options.include_columns = choosed_columns;
    for (const auto& key : choosed_columns) {
      convert_options.column_types[key] = arrow::utf8();
    }
  }

  std::shared_ptr<arrow::csv::StreamingReader> reader;
  PSI_ARROW_GET_RESULT(reader, arrow::csv::StreamingReader::Make(
                                   io_context, infile, read_options,
                                   parse_options, convert_options));
  return reader;
}

std::shared_ptr<arrow::csv::StreamingReader> MakeCsvReader(
    const std::string& path, std::shared_ptr<arrow::Schema> schema) {
  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  PSI_ARROW_GET_RESULT(infile, arrow::io::ReadableFile::Open(
                                   path, arrow::default_memory_pool()));

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  for (const auto& column : schema->fields()) {
    convert_options.include_columns.push_back(column->name());
    convert_options.column_types[column->name()] = column->type();
  }

  std::shared_ptr<arrow::csv::StreamingReader> reader;
  PSI_ARROW_GET_RESULT(reader, arrow::csv::StreamingReader::Make(
                                   io_context, infile, read_options,
                                   parse_options, convert_options));
  return reader;
}

std::vector<std::string> GetCsvColumnsNames(const std::string& path) {
  YACL_ENFORCE(std::filesystem::exists(path), "csv file: {} not exists", path);
  std::ifstream file(path);

  auto tmp_file_name = path + ".temp.csv";
  std::ofstream ofile(tmp_file_name);

  std::string line;
  YACL_ENFORCE(std::getline(file, line), "read csv file first line: {} failed",
               path);
  ofile << line << '\n';
  YACL_ENFORCE(std::getline(file, line), "read csv file second line: {} failed",
               path);
  ofile << line << '\n';
  ofile.close();

  auto reader = MakeCsvReader(tmp_file_name);
  std::vector<std::string> columns_names;
  for (const auto& column : reader->schema()->fields()) {
    columns_names.push_back(column->name());
  }
  auto status = reader->Close();
  YACL_ENFORCE(status.ok(), "close csv reader {} failed", tmp_file_name);

  std::filesystem::remove(tmp_file_name);

  return columns_names;
}

}  // namespace psi
