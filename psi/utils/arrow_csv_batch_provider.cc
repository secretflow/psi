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
#include <future>
#include <mutex>

#include "arrow/array.h"
#include "arrow/compute/api.h"
#include "arrow/datum.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

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
  auto buffer = GetBuffer();
  return buffer.first;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ArrowCsvBatchProvider::ReadNextLabeledBatch() {
  auto buffer = GetBuffer();
  return buffer;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
ArrowCsvBatchProvider::GetBuffer() {
  YACL_ENFORCE(buffer_future_.valid(), "buffer is not valid.");
  auto ret = buffer_future_.get();
  ReadAsync();
  return ret;
}

ArrowCsvBatchProvider::~ArrowCsvBatchProvider() {
  if (buffer_future_.valid()) {
    buffer_future_.wait();
  }
}

void ArrowCsvBatchProvider::ReadAsync() {
  buffer_future_ = std::async([this]() {
    std::vector<std::string> keys;
    keys.reserve(batch_size_);
    std::vector<std::string> labels;
    labels.reserve(batch_size_);

    ReadBatch(&keys, &labels);

    return std::make_pair(keys, labels);
  });
}

void ArrowCsvBatchProvider::ReadBatch(std::vector<std::string>* read_keys,
                                      std::vector<std::string>* read_labels) {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  YACL_ENFORCE(read_keys != nullptr, "read_keys is nullptr.");

  if (reach_eof_) {
    return;
  }

  while (read_keys->size() < batch_size_) {
    if (!buffer_batch_ || idx_in_batch_ >= buffer_batch_->num_rows()) {
      auto status = reader_->ReadNext(&buffer_batch_);
      YACL_ENFORCE(status.ok(), "Read csv file {} error.", file_path_);

      if (buffer_batch_ == nullptr) {
        SPDLOG_INFO("Reach the end of csv file {}.", file_path_);
        reach_eof_ = true;
        return;
      }
      idx_in_batch_ = 0;
      arrays_.clear();

      for (const auto& col : buffer_batch_->columns()) {
        auto str_col = std::dynamic_pointer_cast<arrow::StringArray>(col);
        YACL_ENFORCE(str_col != nullptr, "column type is {}, not string type.",
                     col->type()->ToString());
        arrays_.emplace_back(str_col);
      }
    }

    auto read_cnt = std::min(
        batch_size_ - read_keys->size(),
        static_cast<size_t>(buffer_batch_->num_rows() - idx_in_batch_));

    for (size_t row = 0; row != read_cnt; ++row, ++idx_in_batch_) {
      {
        std::vector<absl::string_view> values;
        values.reserve(keys_.size());
        for (size_t i = 0; i < keys_.size(); i++) {
          values.emplace_back(arrays_[i]->Value(idx_in_batch_));
        }

        read_keys->emplace_back(KeysJoin(values));
      }

      if (read_labels != nullptr && !labels_.empty()) {
        std::vector<absl::string_view> values;
        values.reserve(labels_.size());

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

  ReadAsync();
}

}  // namespace psi
