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

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "arrow/io/api.h"
#include "arrow/ipc/api.h"
#include "yacl/base/exception.h"

#include "psi/algorithm/psi_io.h"

namespace psi {

constexpr char kIdx[] = "psi_index";
constexpr char kPeerCnt[] = "psi_peer_cnt";

constexpr size_t kIndexWriterCacheSize = 1 << 10;

class IndexWriter {
 public:
  explicit IndexWriter(const std::filesystem::path& path,
                       size_t cache_size = kIndexWriterCacheSize,
                       bool trunc = false);

  ~IndexWriter();

  size_t WriteCache(const std::vector<uint64_t>& indexes);

  size_t WriteCache(uint64_t index, uint64_t cnt = 0);

  size_t WriteCache(const std::vector<uint64_t>& indexes,
                    const std::vector<uint64_t>& duplicate_cnt);

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

  std::shared_ptr<arrow::ArrayBuilder> index_builder_;
  std::shared_ptr<arrow::ArrayBuilder> cnt_builder_;

  std::shared_ptr<arrow::io::FileOutputStream> outfile_;

  std::shared_ptr<arrow::ipc::RecordBatchWriter> writer_;

  std::shared_ptr<arrow::Schema> schema_;
};

class IndexReader {
 public:
  IndexReader() = default;

  virtual ~IndexReader() = default;

  virtual bool HasNext() = 0;

  virtual std::optional<uint64_t> GetNext() = 0;

  virtual std::optional<std::pair<uint64_t, uint64_t>> GetNextWithPeerCnt() = 0;

  [[nodiscard]] size_t read_cnt() const { return read_cnt_; }

 protected:
  size_t read_cnt_ = 0;
};

class FileIndexReader : public IndexReader {
 public:
  explicit FileIndexReader(const std::filesystem::path& path);

  bool HasNext() override;

  std::optional<uint64_t> GetNext() override;

  std::optional<std::pair<uint64_t, uint64_t>> GetNextWithPeerCnt() override;

 private:
  std::shared_ptr<arrow::io::ReadableFile> infile_;

  std::shared_ptr<arrow::ipc::RecordBatchReader> reader_;

  std::shared_ptr<arrow::RecordBatch> batch_;

  size_t idx_in_batch_ = 0;

  std::shared_ptr<arrow::UInt64Array> array_;
  std::shared_ptr<arrow::UInt64Array> cnt_array_;
};

class MemoryIndexReader : public IndexReader {
 public:
  explicit MemoryIndexReader(const std::vector<uint32_t>& index,
                             const std::vector<uint32_t>& peer_dup_cnt);

  bool HasNext() override;

  std::optional<uint64_t> GetNext() override;

  std::optional<std::pair<uint64_t, uint64_t>> GetNextWithPeerCnt() override;

 private:
  struct IndexItem {
    uint64_t index;
    uint64_t dup_cnt;
  };

  std::vector<IndexItem> items_;
};

class MemoryResultReceiver : public IResultReceiver {
 public:
  MemoryResultReceiver() = default;
  virtual ~MemoryResultReceiver() = default;

  void Add(PsiResultIndex index) override {
    datas_.emplace_back(std::move(index));
  }
  void Add(std::vector<PsiResultIndex> indices) override {
    datas_.reserve(datas_.size() + indices.size());
    datas_.insert(datas_.end(), std::make_move_iterator(indices.begin()),
                  std::make_move_iterator(indices.end()));
  }

  void Finish() override {}

  std::vector<PsiResultIndex> datas() const { return datas_; }

 private:
  std::vector<PsiResultIndex> datas_;
};

class MemoryResultStore : public IResultStore {
 public:
  MemoryResultStore() : receiver_(std::make_shared<MemoryResultReceiver>()) {}
  ~MemoryResultStore() = default;

  [[nodiscard]] size_t GetBucketNum() const override { return 1; }

  std::shared_ptr<IResultReceiver> GetReceiver(size_t /*tag*/) override {
    return receiver_;
  }

  std::vector<PsiResultIndex> GetData() { return receiver_->datas(); }

 private:
  std::shared_ptr<MemoryResultReceiver> receiver_;
};

}  // namespace psi
