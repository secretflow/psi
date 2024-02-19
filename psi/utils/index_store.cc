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

#include "psi/utils/index_store.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>

#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/csv/options.h"
#include "spdlog/spdlog.h"

namespace psi {

IndexWriter::IndexWriter(const std::filesystem::path& path, size_t cache_size,
                         bool trunc)
    : path_(path), cache_size_(cache_size) {
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = false;
  if (trunc || !std::filesystem::exists(path_)) {
    // NOTE(junfeng): This is a hack to write the header without quotation masks
    // to make YACL csv utils happy.
    // Should be fixed as soon as possible.
    {
      std::ofstream file(path_);
      file << kIdx << "\n";
      file.close();
    }
  }
  outfile_ = arrow::io::FileOutputStream::Open(path_, true).ValueOrDie();

  schema_ = arrow::schema({arrow::field(kIdx, arrow::uint64())});
  writer_ =
      arrow::csv::MakeCSVWriter(outfile_, schema_, write_options).ValueOrDie();

  builder_ = arrow::MakeBuilder(arrow::uint64()).ValueOrDie();
  YACL_ENFORCE(builder_->Resize(cache_size_ * sizeof(uint64_t)).ok());
}

size_t IndexWriter::WriteCache(const std::vector<uint64_t>& indexes) {
  YACL_ENFORCE(!outfile_->closed());

  for (auto i : indexes) {
    WriteCache(i);
  }

  return write_cnt_;
}

size_t IndexWriter::WriteCache(uint64_t index) {
  YACL_ENFORCE(!outfile_->closed());

  YACL_ENFORCE(builder_->AppendScalar(arrow::UInt64Scalar(index)).ok());
  cache_cnt_++;
  write_cnt_++;

  return write_cnt_;
}

void IndexWriter::Commit() {
  YACL_ENFORCE(!outfile_->closed());

  if (cache_cnt_ == 0) {
    return;
  }

  std::vector<std::shared_ptr<arrow::Array>> output_arrays;
  output_arrays.emplace_back(builder_->Finish().ValueOrDie());

  std::shared_ptr<arrow::RecordBatch> output_batch = arrow::RecordBatch::Make(
      schema_, output_arrays[0]->length(), output_arrays);
  if (!writer_->WriteRecordBatch(*output_batch).ok()) {
    YACL_THROW("writer WriteRecordBatch failed.");
  }
  YACL_ENFORCE(outfile_->Flush().ok());
  builder_->Reset();
  cache_cnt_ = 0;
}

void IndexWriter::Close() {
  if (outfile_->closed()) {
    return;
  }

  Commit();

  if (!outfile_->Close().ok()) {
    SPDLOG_ERROR("outfile_ close failed.");
  }
}

IndexWriter::~IndexWriter() { Close(); }

IndexReader::IndexReader(const std::filesystem::path& path) {
  YACL_ENFORCE(std::filesystem::exists(path), "Input file {} doesn't exist.",
               path.string());

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  infile_ = arrow::io::ReadableFile::Open(path, arrow::default_memory_pool())
                .ValueOrDie();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.include_columns = std::vector<std::string>{kIdx};
  reader_ = arrow::csv::StreamingReader::Make(io_context, infile_, read_options,
                                              parse_options, convert_options)
                .ValueOrDie();
}

bool IndexReader::HasNext() {
  bool new_batch = false;
  if (!batch_ || idx_in_batch_ >= static_cast<size_t>(batch_->num_rows())) {
    arrow::Status status = reader_->ReadNext(&batch_);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    new_batch = true;
  }

  if (batch_ == NULL) {
    return false;
  }

  if (new_batch) {
    array_ = std::static_pointer_cast<arrow::UInt64Array>(batch_->column(0));

    idx_in_batch_ = 0;
  }

  return idx_in_batch_ < static_cast<size_t>(batch_->num_rows());
}

std::optional<uint64_t> IndexReader::GetNext() {
  if (!HasNext()) {
    return {};
  } else {
    uint64_t v = array_->Value(idx_in_batch_);
    idx_in_batch_++;
    read_cnt_++;
    return v;
  }
}

}  // namespace psi
