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

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "arrow/io/api.h"
#include "arrow/ipc/api.h"
#include "yacl/base/exception.h"

namespace psi {

constexpr char kIdx[] = "psi_index";

class IndexWriter {
 public:
  explicit IndexWriter(const std::filesystem::path& path,
                       size_t batch_size = 10000, bool trunc = false);

  ~IndexWriter();

  size_t WriteCache(const std::vector<uint64_t>& indexes);

  size_t WriteCache(uint64_t index);

  void Close();

  void Commit();

  [[nodiscard]] size_t cache_cnt() const { return cache_cnt_; }

  [[nodiscard]] size_t cache_size() const { return cache_size_; }

  [[nodiscard]] size_t write_cnt() const { return write_cnt_; }

  [[nodiscard]] std::filesystem::path path() const { return path_; }

 private:
  std::filesystem::path path_;

  size_t cache_cnt_ = 0;

  size_t write_cnt_ = 0;

  size_t cache_size_ = 0;

  std::shared_ptr<arrow::ArrayBuilder> builder_;

  std::shared_ptr<arrow::io::FileOutputStream> outfile_;

  std::shared_ptr<arrow::ipc::RecordBatchWriter> writer_;

  std::shared_ptr<arrow::Schema> schema_;
};

class IndexReader {
 public:
  explicit IndexReader(const std::filesystem::path& path);

  bool HasNext();

  std::optional<uint64_t> GetNext();

  [[nodiscard]] size_t read_cnt() const { return read_cnt_; }

 private:
  std::shared_ptr<arrow::io::ReadableFile> infile_;

  std::shared_ptr<arrow::ipc::RecordBatchReader> reader_;

  std::shared_ptr<arrow::RecordBatch> batch_;

  size_t idx_in_batch_ = 0;

  size_t read_cnt_ = 0;

  std::shared_ptr<arrow::UInt64Array> array_;
};

}  // namespace psi
