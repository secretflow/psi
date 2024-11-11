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

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "arrow/csv/api.h"
#include "arrow_csv_batch_provider.h"

#include "psi/utils/batch_provider.h"
#include "psi/utils/index_store.h"

#include "psi/utils/table_utils.pb.h"

namespace psi {

constexpr size_t kBatchSize = 1 << 16;

std::string MakeQuotedCsvLine(const std::vector<std::string>& columns);

class Table {
 public:
  static std::shared_ptr<Table> MakeFromCsv(const std::string& path);

  virtual ~Table() = default;

  std::string Path() const { return path_; }

  std::string Format() const { return format_; }

  void Sort(std::vector<std::string> keys, std::string new_path) const;
  void SortInplace(std::vector<std::string> keys) const;

  virtual std::vector<std::string> Columns() const { return columns_; }

  void CheckColumnsInTable(const std::vector<std::string>& columns) const;

  virtual std::shared_ptr<IBasicBatchProvider> GetProvider(
      std::vector<std::string> choosed_columns,
      size_t batch_size = kBatchSize) const;

 protected:
  explicit Table(std::string path, std::string format);

  std::string path_;
  std::string format_;
  std::vector<std::string> columns_;
};

class TableWithKeys : public Table {
 public:
  using Table::Table;
  virtual std::vector<std::string> Keys() const = 0;
  virtual std::string TypeName() const = 0;
};

class UniqueKeyTable : public TableWithKeys {
 public:
  static std::shared_ptr<UniqueKeyTable> Make(
      std::string path, std::string format,
      const std::vector<std::string>& keys) {
    auto unique_key_table = std::shared_ptr<UniqueKeyTable>(
        new UniqueKeyTable(std::move(path), std::move(format), keys));
    auto columns = unique_key_table->Columns();

    unique_key_table->CheckColumnsInTable(keys);

    return unique_key_table;
  }

  std::vector<std::string> Keys() const override { return keys_; }

  std::string TypeName() const override { return "UniqueKeyTable"; }

 protected:
  UniqueKeyTable(std::string path, std::string format,
                 std::vector<std::string> keys);

  std::vector<std::string> keys_;
};

class SortedTable : public TableWithKeys {
 public:
  static std::shared_ptr<SortedTable> Make(
      std::shared_ptr<Table> origin, const std::string& new_path,
      const std::vector<std::string>& keys);
  static std::shared_ptr<SortedTable> Make(
      const std::string& new_path, const std::vector<std::string>& keys);

  std::vector<std::string> Keys() const override { return keys_; }

  std::string TypeName() const override { return "SortedTable"; }

 protected:
  SortedTable(const std::string& path, std::string format,
              const std::vector<std::string>& keys);

  std::vector<std::string> keys_;
};

struct ResultDumper {
 public:
  ResultDumper(std::string intersect_path, std::string except_path);
  void ToIntersect(const std::string& line, int64_t duplicate_cnt = 0);
  void ToExcept(const std::string& line, int64_t duplicate_cnt = 0);

  int64_t except_cnt() const { return except_cnt_; }
  int64_t intersect_cnt() const { return intersect_cnt_; }

 private:
  void Dump(const std::string& line, int64_t duplicate_cnt,
            std::shared_ptr<std::ofstream>& file, int64_t* total_dump_cnt);

  // empty indicate no output
  std::string intersect_path_;
  std::string except_path_;
  std::shared_ptr<std::ofstream> intersect_file_;
  std::shared_ptr<std::ofstream> except_file_;
  int64_t intersect_cnt_ = 0;
  int64_t except_cnt_ = 0;
};

class KeysInfoProvider : public IBasicBatchProvider {
 public:
  struct BatchInfo {
    std::vector<std::string> keys;
    std::vector<uint32_t> start_indexes;
    std::vector<uint32_t> dup_cnts;
  };

  explicit KeysInfoProvider(std::string path, size_t batch_size)
      : path_(std::move(path)), batch_size_(batch_size) {}

  size_t batch_size() const override { return batch_size_; }

  virtual BatchInfo ReadBatchWithInfo() = 0;

  virtual ~KeysInfoProvider() = default;

 protected:
  std::string path_;

  int64_t batch_index_ = 0;
  int64_t batch_size_;
};

class UniqueTableKeysInfoProvider : public KeysInfoProvider {
 public:
  UniqueTableKeysInfoProvider(std::string path,
                              const std::vector<std::string>& keys,
                              size_t batch_size);

  // Read at most `batch_size` items and return them. An empty returned vector
  // is treated as the end of stream.
  std::vector<std::string> ReadNextBatch() override;

  // key, dup_cnt
  std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() override;

  BatchInfo ReadBatchWithInfo() override;

 private:
  std::vector<std::string> keys_;
  std::shared_ptr<ArrowCsvBatchProvider> provider_;
  uint32_t read_index_ = 0;
};

class SortedTableKeysInfoProvider : public KeysInfoProvider {
 public:
  explicit SortedTableKeysInfoProvider(std::string path, size_t batch_size);

  ~SortedTableKeysInfoProvider() override;
  // Read at most `batch_size` items and return them. An empty returned vector
  // is treated as the end of stream.
  std::vector<std::string> ReadNextBatch() override;

  // key, dup_cnt
  std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() override;

  BatchInfo ReadBatchWithInfo() override;

 private:
  void ReadFutureBatch();

  std::shared_ptr<arrow::RecordBatch> GetBatch();

  std::future<std::shared_ptr<arrow::RecordBatch>> read_future_;

  std::shared_ptr<arrow::RecordBatch> batch_;

  std::shared_ptr<arrow::csv::StreamingReader> reader_;
  int64_t batch_index_ = 0;
  std::shared_ptr<arrow::StringArray> keys_col_;
  std::shared_ptr<arrow::Int64Array> start_index_col_;
  std::shared_ptr<arrow::Int64Array> dup_cnt_col_;
};

class KeyInfo : public Table {
 public:
  struct StatInfo {
    // for example: self table is {"id":["1","2","2","3","4","4"]]}
    // add peer table is {"id":["2","2","2","3"]}
    // self_intersection_count is 3, ["2","2","3"]
    uint32_t self_intersection_count = 0;
    // peer_intersection_count is 4, ["2","2","2","3"]
    uint32_t peer_intersection_count = 0;
    // original_count is 6, ["1","2","2","3","4","4"]
    uint32_t original_count = 0;
    // join_intersection_count is 7 = 2 * 3 + 1
    uint32_t join_intersection_count = 0;
    // inter_unique_cnt is 2: ["2","3"]
    uint32_t inter_unique_cnt = 0;

    std::string ToString() const;
  };

  struct Option {
    std::vector<uint8_t> keys_hash;
    uint32_t duplicate_key_cnt = 0;
    uint32_t unique_key_cnt = 0;
    uint32_t original_cnt = 0;
    uint64_t source_file_size = 0;

    void Load(const std::string& path);
    void Save(const std::string& path);
  };

  inline static const std::string kKey = "psi_joined_key";
  inline static const std::string kStartIndex = "psi_start_index";
  inline static const std::string kDupCnt = "psi_dup_cnt";

  static std::shared_ptr<KeyInfo> Make(
      std::shared_ptr<SortedTable> sorted_table, std::string path);

  static std::shared_ptr<KeyInfo> Make(
      std::shared_ptr<UniqueKeyTable> unique_key_table);

  std::vector<std::string> Columns() const override {
    return std::vector<std::string>{kKey, kStartIndex, kDupCnt};
  }

  std::shared_ptr<IBasicBatchProvider> GetKeysProviderWithDupCnt(
      size_t batch_size = kBatchSize) const;

  std::shared_ptr<KeysInfoProvider> GetBatchProvider(
      size_t batch_size = kBatchSize) const;

  std::vector<uint8_t> KeysHash() const {
    return std::vector<uint8_t>(meta_.keys_hash().begin(),
                                meta_.keys_hash().end());
  }

  uint32_t DupKeyCnt() const { return meta_.duplicate_key_cnt(); }

  uint32_t KeyCnt() const { return meta_.unique_key_cnt(); }
  uint32_t OriginCnt() const { return meta_.original_cnt(); }

  // assume first col is index:int64, second col is peer_cnt:int64
  StatInfo ApplyPeerDupCnt(IndexReader& reader, ResultDumper& dumper);

  std::shared_ptr<arrow::csv::StreamingReader> GetStreamReader() const;

  static std::shared_ptr<arrow::Schema> Schema();

  std::vector<std::string> SourceFileColumns() const;

 protected:
  explicit KeyInfo(std::string path, std::string format,
                   std::shared_ptr<TableWithKeys> table_with_keys,
                   proto::KeyInfoMeta opts);
  std::shared_ptr<TableWithKeys> table_;
  proto::KeyInfoMeta meta_;
};

class InterIndexProcessor {
 public:
  struct InterInfo {
    InterInfo() = default;
    InterInfo(uint32_t index, uint32_t self_cnt, uint32_t cnt)
        : start_index(index), self_dup_cnt(self_cnt), peer_dup_cnt(cnt) {}

    uint32_t start_index;
    uint32_t self_dup_cnt;
    uint32_t peer_dup_cnt;
  };

  InterIndexProcessor(IndexReader& reader,
                      std::shared_ptr<KeysInfoProvider> self_info_provider);

  std::vector<InterInfo> GetBatchInfo();

  bool IsFinish() const { return finish_; }

 private:
  IndexReader& reader_;
  std::shared_ptr<KeysInfoProvider> self_info_provider_;
  std::optional<std::pair<uint64_t, uint64_t>> index_with_peer_cnt_;
  uint32_t self_info_index_ = 0;
  bool finish_ = false;
};

}  // namespace psi
