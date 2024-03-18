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

#include "psi/utils/arrow_csv_batch_provider.h"

#include <cassert>
#include <filesystem>

#include "arrow/array.h"
#include "arrow/compute/api.h"
#include "arrow/datum.h"
#include "spdlog/spdlog.h"

#include "psi/utils/key.h"

namespace psi {

ArrowCsvBatchProvider::ArrowCsvBatchProvider(
    const std::string& file_path, const std::vector<std::string>& keys,
    size_t batch_size, const std::vector<std::string>& labels)
    : batch_size_(batch_size),
      file_path_(file_path),
      keys_(keys),
      labels_(labels) {
  Init();
}

std::vector<std::string> ArrowCsvBatchProvider::ReadNextBatch() {
  std::vector<std::string> res;

  ReadNextBatch(&res);

  return res;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ArrowCsvBatchProvider::ReadNextLabeledBatch() {
  std::vector<std::string> read_keys;
  std::vector<std::string> read_labels;

  ReadNextBatch(&read_keys, &read_labels);

  return std::make_pair(read_keys, read_labels);
}

void ArrowCsvBatchProvider::ReadNextBatch(
    std::vector<std::string>* read_keys,
    std::vector<std::string>* read_labels) {
  assert(read_keys);

  while (read_keys->size() < batch_size_) {
    bool new_batch = false;

    if (!batch_ || idx_in_batch_ >= batch_->num_rows()) {
      arrow::Status status = reader_->ReadNext(&batch_);
      if (!status.ok()) {
        YACL_THROW("Read csv error.");
      }

      new_batch = true;
    }

    if (!batch_) {
      SPDLOG_INFO("Reach the end of csv file {}.", file_path_);
      return;
    }

    if (new_batch) {
      idx_in_batch_ = 0;

      arrays_.clear();

      for (const auto& col : batch_->columns()) {
        arrays_.emplace_back(
            std::dynamic_pointer_cast<arrow::StringArray>(col));
      }
    }

    for (;
         idx_in_batch_ < batch_->num_rows() && read_keys->size() < batch_size_;
         idx_in_batch_++) {
      {
        std::vector<absl::string_view> values;
        for (size_t i = 0; i < keys_.size(); i++) {
          values.emplace_back(arrays_[i]->Value(idx_in_batch_));
        }

        read_keys->emplace_back(KeysJoin(values));
      }

      if (read_labels) {
        std::vector<absl::string_view> values;
        for (size_t i = keys_.size(); i < arrays_.size(); i++) {
          values.emplace_back(arrays_[i]->Value(idx_in_batch_));
        }

        read_labels->emplace_back(KeysJoin(values));
      }
      row_cnt_++;
    }
  }
}

void ArrowCsvBatchProvider::Init() {
  YACL_ENFORCE(std::filesystem::exists(file_path_),
               "Input file {} doesn't exist.", file_path_);

  YACL_ENFORCE(!keys_.empty(), "You must provide keys.");

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  infile_ =
      arrow::io::ReadableFile::Open(file_path_, arrow::default_memory_pool())
          .ValueOrDie();

  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();

  for (const auto& key : keys_) {
    convert_options.column_types[key] = arrow::utf8();
  }
  for (const auto& label : labels_) {
    convert_options.column_types[label] = arrow::utf8();
  }
  convert_options.include_columns = keys_;
  convert_options.include_columns.insert(convert_options.include_columns.end(),
                                         labels_.begin(), labels_.end());

  reader_ = arrow::csv::StreamingReader::Make(io_context, infile_, read_options,
                                              parse_options, convert_options)
                .ValueOrDie();
}

}  // namespace psi
