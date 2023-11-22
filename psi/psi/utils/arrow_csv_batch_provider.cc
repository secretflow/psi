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

#include "psi/psi/utils/arrow_csv_batch_provider.h"

#include <filesystem>

#include "arrow/array.h"
#include "arrow/compute/api.h"
#include "arrow/datum.h"
#include "spdlog/spdlog.h"

namespace psi::psi {

ArrowCsvBatchProvider::ArrowCsvBatchProvider(
    const std::string& file_path, const std::vector<std::string>& keys,
    const std::string& separator, size_t block_size)
    : block_size_(block_size),
      file_path_(file_path),
      keys_(keys),
      separator_(separator) {
  Init();
}

std::vector<std::string> ArrowCsvBatchProvider::ReadNextBatch() {
  std::shared_ptr<arrow::RecordBatch> batch;
  arrow::Status status = reader_->ReadNext(&batch);
  if (!status.ok()) {
    YACL_THROW("Read csv error.");
  }

  if (!batch) {
    SPDLOG_INFO("Reach the end of csv file {}.", file_path_);
    return {};
  }

  std::vector<arrow::Datum> join_cols;

  arrow::compute::CastOptions cast_options;

  for (const auto& col : batch->columns()) {
    join_cols.emplace_back(
        arrow::compute::Cast(arrow::Datum(*col), arrow::utf8(), cast_options)
            .ValueOrDie());
  }

  join_cols.emplace_back(arrow::MakeScalar(separator_));

  arrow::Datum join_datum =
      arrow::compute::CallFunction("binary_join_element_wise", join_cols)
          .ValueOrDie();

  std::shared_ptr<arrow::Array> join_array = std::move(join_datum).make_array();
  auto str_array = std::dynamic_pointer_cast<arrow::StringArray>(join_array);

  std::vector<std::string> res;
  for (int i = 0; i < batch->num_rows(); ++i) {
    res.emplace_back(str_array->GetString(i));
  }

  row_cnt_ += batch->num_rows();

  return res;
}

void ArrowCsvBatchProvider::Init() {
  YACL_ENFORCE(std::filesystem::exists(file_path_),
               "Input file {} doesn't exist.", file_path_);

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  infile_ =
      arrow::io::ReadableFile::Open(file_path_, arrow::default_memory_pool())
          .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  read_options.block_size = block_size_;
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  if (!keys_.empty()) {
    convert_options.include_columns = keys_;
  }

  reader_ = arrow::csv::StreamingReader::Make(io_context, infile_, read_options,
                                              parse_options, convert_options)
                .ValueOrDie();
}

}  // namespace psi::psi
