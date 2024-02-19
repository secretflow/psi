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

#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "yacl/base/exception.h"

namespace psi {

CsvHeaderParser::CsvHeaderParser(const std::string& path) : path_(path) {
  YACL_ENFORCE(std::filesystem::exists(path_), "Input file {} doesn't exist.",
               path_);

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;
  infile = arrow::io::ReadableFile::Open(path_, arrow::default_memory_pool())
               .ValueOrDie();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  auto reader =
      arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                        parse_options, convert_options)
          .ValueOrDie();

  const std::shared_ptr<arrow::Schema>& schema = reader->schema();

  for (int i = 0; i < schema->num_fields(); i++) {
    key_index_map_[schema->field(i)->name()] = i;
  }

  if (!infile->Close().ok()) {
    YACL_THROW("Infile {} close failed.", path_);
  }
}

std::vector<size_t> CsvHeaderParser::target_indices(
    const std::vector<std::string>& target_fields, size_t offset) const {
  std::vector<size_t> indices;

  for (const std::string& key : target_fields) {
    if (key_index_map_.find(key) == key_index_map_.end()) {
      YACL_THROW("key {} is not found in {}", key, path_);
    }

    indices.emplace_back(key_index_map_.at(key) + offset);
  }

  return indices;
}

}  // namespace psi
