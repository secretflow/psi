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
#include <utility>

#include "absl/strings/str_join.h"
#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/csv/options.h"
#include "spdlog/spdlog.h"

#include "psi/utils/io.h"

namespace psi {

IndexWriter::IndexWriter(const std::filesystem::path& path, size_t cache_size,
                         bool trunc)
    : path_(path), cache_size_(cache_size) {
  std::vector<std::string> fields = {kIdx, kPeerCnt};
  if (trunc || !std::filesystem::exists(path_)) {
    // NOTE(junfeng): This is a hack to write the header without quotation masks
    // to make YACL csv utils happy.
    // Should be fixed as soon as possible.
    {
      std::ofstream file(path_);

      file << absl::StrJoin(fields, ",") << '\n';
      file.close();
    }
  }
  outfile_ = io::GetArrowOutputStream(path_, true);

  schema_ = arrow::schema({arrow::field(kIdx, arrow::uint64()),
                           arrow::field(kPeerCnt, arrow::uint64())});
  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = false;
  writer_ =
      arrow::csv::MakeCSVWriter(outfile_, schema_, write_options).ValueOrDie();

  index_builder_ = arrow::MakeBuilder(arrow::uint64()).ValueOrDie();
  cnt_builder_ = arrow::MakeBuilder(arrow::uint64()).ValueOrDie();
  YACL_ENFORCE(index_builder_->Resize(cache_size_ * sizeof(uint64_t)).ok());
  YACL_ENFORCE(cnt_builder_->Resize(cache_size_ * sizeof(uint64_t)).ok());
}

size_t IndexWriter::WriteCache(const std::vector<uint64_t>& indexes,
                               const std::vector<uint64_t>& duplicate_cnt) {
  YACL_ENFORCE(!outfile_->closed());

  for (size_t i = 0; i < indexes.size(); i++) {
    WriteCache(indexes[i], duplicate_cnt[i]);
  }

  return write_cnt_;
}

size_t IndexWriter::WriteCache(const std::vector<uint64_t>& indexes) {
  YACL_ENFORCE(!outfile_->closed());

  for (auto i : indexes) {
    WriteCache(i);
  }

  return write_cnt_;
}

size_t IndexWriter::WriteCache(uint64_t index, uint64_t cnt) {
  YACL_ENFORCE(!outfile_->closed());

  YACL_ENFORCE(index_builder_->AppendScalar(arrow::UInt64Scalar(index)).ok());
  YACL_ENFORCE(cnt_builder_->AppendScalar(arrow::UInt64Scalar(cnt)).ok());
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
  output_arrays.emplace_back(index_builder_->Finish().ValueOrDie());
  output_arrays.emplace_back(cnt_builder_->Finish().ValueOrDie());

  std::shared_ptr<arrow::RecordBatch> output_batch = arrow::RecordBatch::Make(
      schema_, output_arrays[0]->length(), output_arrays);
  if (!writer_->WriteRecordBatch(*output_batch).ok()) {
    YACL_THROW("writer WriteRecordBatch failed.");
  }
  YACL_ENFORCE(outfile_->Flush().ok());
  index_builder_->Reset();
  cnt_builder_->Reset();
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

FileIndexReader::FileIndexReader(const std::filesystem::path& path) {
  YACL_ENFORCE(std::filesystem::exists(path), "Input file {} doesn't exist.",
               path.string());

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  infile_ = arrow::io::ReadableFile::Open(path, arrow::default_memory_pool())
                .ValueOrDie();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.include_columns = std::vector<std::string>{kIdx, kPeerCnt};
  reader_ = arrow::csv::StreamingReader::Make(io_context, infile_, read_options,
                                              parse_options, convert_options)
                .ValueOrDie();
}

bool FileIndexReader::HasNext() {
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
    cnt_array_ =
        std::static_pointer_cast<arrow::UInt64Array>(batch_->column(1));

    idx_in_batch_ = 0;
  }

  return idx_in_batch_ < static_cast<size_t>(batch_->num_rows());
}

std::optional<uint64_t> FileIndexReader::GetNext() {
  if (!HasNext()) {
    return {};
  } else {
    uint64_t v = array_->Value(idx_in_batch_);
    idx_in_batch_++;
    read_cnt_++;
    return v;
  }
}

std::optional<std::pair<uint64_t, uint64_t>>
FileIndexReader::GetNextWithPeerCnt() {
  if (!HasNext()) {
    return {};
  } else {
    uint64_t v = array_->Value(idx_in_batch_);
    uint64_t c = cnt_array_->Value(idx_in_batch_);
    idx_in_batch_++;
    read_cnt_++;
    SPDLOG_DEBUG("index: {}, {}, idx_in_batch_ {}", v, c, idx_in_batch_);
    return std::make_pair(v, c);
  }
}

MemoryIndexReader::MemoryIndexReader(
    const std::vector<uint32_t>& index,
    const std::vector<uint32_t>& peer_dup_cnt) {
  YACL_ENFORCE(index.size() == peer_dup_cnt.size());
  items_.resize(index.size());
  for (size_t i = 0; i != index.size(); ++i) {
    items_[i].index = index[i];
    items_[i].dup_cnt = peer_dup_cnt[i];
  }
  std::sort(items_.begin(), items_.end(),
            [](const auto& a, const auto& b) { return a.index < b.index; });
}

bool MemoryIndexReader::HasNext() { return read_cnt_ < items_.size(); }

std::optional<uint64_t> MemoryIndexReader::GetNext() {
  if (!HasNext()) {
    return {};
  }
  return items_[read_cnt_++].index;
}

std::optional<std::pair<uint64_t, uint64_t>>
MemoryIndexReader::GetNextWithPeerCnt() {
  if (!HasNext()) {
    return {};
  }
  auto ret = std::pair(items_[read_cnt_].index, items_[read_cnt_].dup_cnt);
  read_cnt_++;
  return ret;
}

}  // namespace psi
