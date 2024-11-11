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

#include "psi/utils/table_utils.h"

#include <spdlog/spdlog.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/hash/ssl_hash.h"

#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/arrow_helper.h"
#include "psi/utils/index_store.h"
#include "psi/utils/io.h"
#include "psi/utils/key.h"
#include "psi/utils/pb_helper.h"

#include "psi/utils/table_utils.pb.h"

namespace psi {

UniqueKeyTable::UniqueKeyTable(std::string path, std::string format,
                               std::vector<std::string> keys)
    : TableWithKeys(std::move(path), std::move(format)),
      keys_(std::move(keys)) {}

SortedTableKeysInfoProvider::SortedTableKeysInfoProvider(std::string path,
                                                         size_t batch_size)
    : KeysInfoProvider(std::move(path), batch_size) {
  reader_ = MakeCsvReader(path_, KeyInfo::Schema());
  ReadFutureBatch();
};

// Read at most `batch_size` items and return them. An empty returned vector
// is treated as the end of stream.
std::vector<std::string> SortedTableKeysInfoProvider::ReadNextBatch() {
  return ReadNextBatchWithDupCnt().first;
}

std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
SortedTableKeysInfoProvider::ReadNextBatchWithDupCnt() {
  auto batch_info = ReadBatchWithInfo();

  std::unordered_map<uint32_t, uint32_t> duplicate_cnt;
  auto& dup_cnt = batch_info.dup_cnts;
  for (uint32_t i = 0; i != dup_cnt.size(); ++i) {
    if (dup_cnt[i] != 0) {
      duplicate_cnt[i] = dup_cnt[i];
    }
  }
  return {batch_info.keys, duplicate_cnt};
}

KeysInfoProvider::BatchInfo SortedTableKeysInfoProvider::ReadBatchWithInfo() {
  BatchInfo batch_info;
  int64_t read_cnt = 0;
  while (read_cnt < batch_size_) {
    if (batch_ == nullptr || batch_index_ >= batch_->num_rows()) {
      batch_ = GetBatch();
      if (batch_ == nullptr) {
        SPDLOG_INFO("reach end of stream of {}, {} lines", path_, read_cnt);
        break;
      }
      keys_col_ =
          std::dynamic_pointer_cast<arrow::StringArray>(batch_->column(0));
      YACL_ENFORCE(keys_col_ != nullptr);
      start_index_col_ =
          std::dynamic_pointer_cast<arrow::Int64Array>(batch_->column(1));
      YACL_ENFORCE(start_index_col_ != nullptr);
      dup_cnt_col_ =
          std::dynamic_pointer_cast<arrow::Int64Array>(batch_->column(2));
      YACL_ENFORCE(dup_cnt_col_ != nullptr);

      batch_index_ = 0;
    }

    auto cur_read_cnt =
        std::min(batch_->num_rows() - batch_index_, batch_size_ - read_cnt);
    for (uint32_t i = 0; i < cur_read_cnt; ++i) {
      batch_info.keys.push_back(keys_col_->GetString(batch_index_));
      batch_info.start_indexes.push_back(start_index_col_->Value(batch_index_));
      batch_info.dup_cnts.push_back(dup_cnt_col_->Value(batch_index_));
      ++batch_index_;
      ++read_cnt;
      if (read_cnt >= batch_size_) {
        break;
      }
    }
  }
  SPDLOG_INFO("read {} lines from {}", batch_info.keys.size(), path_);
  return batch_info;
}

SortedTableKeysInfoProvider::~SortedTableKeysInfoProvider() {
  if (read_future_.valid()) {
    read_future_.get();
  }
}

void SortedTableKeysInfoProvider::ReadFutureBatch() {
  read_future_ = std::async([this]() {
    std::shared_ptr<arrow::RecordBatch> batch;
    auto status = reader_->ReadNext(&batch);
    YACL_ENFORCE(status.ok(), "read csv {} failed", path_);
    return batch;
  });
}

std::shared_ptr<arrow::RecordBatch> SortedTableKeysInfoProvider::GetBatch() {
  auto read_batch = [this]() {
    std::shared_ptr<arrow::RecordBatch> batch;
    auto status = reader_->ReadNext(&batch);
    YACL_ENFORCE(status.ok(), "read csv {} failed", path_);
    return batch;
  };
  std::shared_ptr<arrow::RecordBatch> batch;
  if (read_future_.valid()) {
    batch = read_future_.get();
    read_future_ = std::async(read_batch);
  } else {
    batch = read_batch();
    read_future_ = std::async(read_batch);
  }
  return batch;
}

UniqueTableKeysInfoProvider::UniqueTableKeysInfoProvider(
    std::string path, const std::vector<std::string>& keys, size_t batch_size)
    : KeysInfoProvider(std::move(path), batch_size), keys_(keys) {
  provider_ = std::make_shared<ArrowCsvBatchProvider>(path_, keys_, batch_size);
};

// Read at most `batch_size` items and return them. An empty returned vector
// is treated as the end of stream.
std::vector<std::string> UniqueTableKeysInfoProvider::ReadNextBatch() {
  return ReadNextBatchWithDupCnt().first;
}

std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
UniqueTableKeysInfoProvider::ReadNextBatchWithDupCnt() {
  auto items = provider_->ReadNextBatch();
  return {items, {}};
}

KeysInfoProvider::BatchInfo UniqueTableKeysInfoProvider::ReadBatchWithInfo() {
  KeysInfoProvider::BatchInfo batch_info;
  batch_info.keys = provider_->ReadNextBatch();
  batch_info.start_indexes.resize(batch_info.keys.size());
  std::iota(batch_info.start_indexes.begin(), batch_info.start_indexes.end(),
            read_index_);
  read_index_ += batch_info.start_indexes.size();
  batch_info.dup_cnts.assign(batch_info.start_indexes.size(), 0);
  return batch_info;
}

std::shared_ptr<Table> Table::MakeFromCsv(const std::string& path) {
  return std::shared_ptr<Table>(new Table(path, "csv"));
}

std::string KeyInfo::StatInfo::ToString() const {
  std::ostringstream oss;
  oss << "{ self_intersection_count: " << self_intersection_count
      << ", peer_intersection_count: " << peer_intersection_count
      << ", original_count: " << original_count
      << ", inter_unique_cnt: " << inter_unique_cnt
      << ", join_intersection_count: " << join_intersection_count << " }";
  return oss.str();
}

Table::Table(std::string path, std::string format)
    : path_(std::move(path)), format_(std::move(format)) {
  YACL_ENFORCE(std::filesystem::exists(path_), "file {} not exists", path_);
  YACL_ENFORCE(format_ == "csv", "only support csv format for now");
  YACL_ENFORCE(std::filesystem::file_size(path_) > 0, "file {} is empty",
               path_);
  SPDLOG_INFO("Init table with file: {}, size: {}, format: {}", path_,
              std::filesystem::file_size(path_), format_);
  columns_ = GetCsvColumnsNames(path_);
  SPDLOG_INFO("table header: {}", MakeQuotedCsvLine(columns_));
}

std::string MakeQuotedCsvLine(const std::vector<std::string>& columns) {
  std::ostringstream oss;
  oss << '"' << columns[0] << '"';
  for (size_t i = 1; i < columns.size(); ++i) {
    oss << ',' << '"' << columns[i] << '"';
  }
  return oss.str();
}

std::shared_ptr<IBasicBatchProvider> Table::GetProvider(
    std::vector<std::string> choosed_columns, size_t batch_size) const {
  if (format_ == "csv") {
    return std::make_shared<ArrowCsvBatchProvider>(path_, choosed_columns,
                                                   batch_size);
  } else {
    YACL_THROW("not support format {}", format_);
  }
}

void Table::SortInplace(std::vector<std::string> keys) const {
  auto new_name = path_ + ".sorted";
  Sort(keys, new_name);
  std::filesystem::remove(path_);
  std::filesystem::rename(new_name, path_);
}

void Table::Sort(std::vector<std::string> keys, std::string new_path) const {
  YACL_ENFORCE(format_ == "csv", "only support csv format for now");
  auto new_name = path_ + ".sorted";
  MultiKeySort(path_, new_path, keys);
}

void Table::CheckColumnsInTable(const std::vector<std::string>& columns) const {
  std::unordered_set<std::string> table_cols(columns_.begin(), columns_.end());
  for (const auto& col : columns) {
    YACL_ENFORCE(table_cols.find(col) != table_cols.end(),
                 "columns {} not found in table: {}", col, Path());
  }
}

std::shared_ptr<SortedTable> SortedTable::Make(
    std::shared_ptr<Table> origin, const std::string& new_path,
    const std::vector<std::string>& keys) {
  if (!std::filesystem::exists(new_path)) {
    origin->Sort(keys, new_path);
  } else {
    SPDLOG_INFO("sort file {} exists already.", new_path);
  }
  return std::shared_ptr<SortedTable>(
      new SortedTable(new_path, origin->Format(), keys));
}
std::shared_ptr<SortedTable> SortedTable::Make(
    const std::string& new_path, const std::vector<std::string>& keys) {
  YACL_ENFORCE(std::filesystem::exists(new_path),
               "sort file {} does not exist.", new_path);
  return std::shared_ptr<SortedTable>(new SortedTable(new_path, "csv", keys));
}

SortedTable::SortedTable(const std::string& path, std::string format,
                         const std::vector<std::string>& keys)
    : TableWithKeys(path, std::move(format)), keys_(keys) {}

std::shared_ptr<arrow::Schema> KeyInfo::Schema() {
  return arrow::schema({arrow::field(KeyInfo::kKey, arrow::utf8()),
                        arrow::field(KeyInfo::kStartIndex, arrow::int64()),
                        arrow::field(KeyInfo::kDupCnt, arrow::int64())});
}

std::shared_ptr<KeyInfo> KeyInfo::Make(
    std::shared_ptr<SortedTable> sorted_table, std::string path) {
  auto meta_path = path + ".meta";
  if (std::filesystem::exists(path) && std::filesystem::exists(meta_path) &&
      std::filesystem::file_size(meta_path)) {
    SPDLOG_INFO("meta file {} exists already.", meta_path);
    proto::KeyInfoMeta meta;
    LoadJsonFileToPbMessage(meta_path, meta);
    if (meta.source_file_size() ==
        std::filesystem::file_size(sorted_table->Path())) {
      return std::shared_ptr<KeyInfo>(
          new KeyInfo(path, "csv", sorted_table, meta));
    } else {
      SPDLOG_WARN("Sorted file size {} not match meta file size {}, rebuild.",
                  std::filesystem::file_size(sorted_table->Path()),
                  meta.source_file_size());
    }
  }

  std::ofstream out(path);
  yacl::crypto::Sha256Hash hash;
  uint32_t duplicate_key_cnt = 0;
  uint32_t unique_key_cnt = 0;
  uint32_t origin_line_cnt = 0;

  out << absl::StrJoin({kKey, kStartIndex, kDupCnt}, ",") << '\n';
  auto write_to_csv = [&](std::vector<std::string> keys,
                          std::vector<uint32_t> start_index,
                          std::vector<uint32_t> dup_cnts) {
    for (size_t i = 0; i < keys.size(); ++i) {
      hash.Update(keys[i]);
      out << '"' << keys[i] << '"' << ',' << start_index[i] << ','
          << dup_cnts[i] << '\n';
      if (dup_cnts[i] != 0) {
        duplicate_key_cnt++;
      }
      unique_key_cnt++;
      origin_line_cnt += 1 + dup_cnts[i];
    }
  };

  auto provider = sorted_table->GetProvider(sorted_table->Keys());
  std::future<void> write_future;
  uint32_t cur_key_start_index = 0;
  uint32_t table_index = 0;
  uint32_t cur_key_dup_cnt = 0;
  auto batch = provider->ReadNextBatch();
  std::string cur_key;
  while (!batch.empty()) {
    std::vector<std::string> keys;
    std::vector<uint32_t> start_index;
    std::vector<uint32_t> dup_cnts;
    for (auto& item : batch) {
      if (table_index == 0) {
        cur_key = item;
        table_index++;
        continue;
      }
      if (cur_key != item) {
        keys.push_back(cur_key);
        start_index.push_back(cur_key_start_index);
        dup_cnts.push_back(cur_key_dup_cnt);
        cur_key = item;
        cur_key_start_index = table_index;
        cur_key_dup_cnt = 0;
      } else {
        cur_key_dup_cnt++;
      }
      table_index++;
    }
    if (write_future.valid()) {
      write_future.get();
    }
    write_future = std::async(std::launch::async, write_to_csv, std::move(keys),
                              std::move(start_index), std::move(dup_cnts));
    batch = provider->ReadNextBatch();
  }
  if (write_future.valid()) {
    write_future.get();
  }

  write_to_csv({cur_key}, {cur_key_start_index}, {cur_key_dup_cnt});
  out.close();

  proto::KeyInfoMeta meta;
  auto keys_hash = hash.CumulativeHash();
  meta.mutable_keys_hash()->assign(keys_hash.begin(), keys_hash.end());
  meta.set_duplicate_key_cnt(duplicate_key_cnt);
  meta.set_unique_key_cnt(unique_key_cnt);
  meta.set_original_cnt(origin_line_cnt);
  meta.set_source_file_size(std::filesystem::file_size(sorted_table->Path()));
  DumpPbMessageToJsonFile(meta, meta_path);

  return std::shared_ptr<KeyInfo>(new KeyInfo(path, "csv", sorted_table, meta));
}

std::shared_ptr<KeyInfo> KeyInfo::Make(
    std::shared_ptr<UniqueKeyTable> unique_key_table) {
  auto meta_path = unique_key_table->Path() + ".meta";
  if (std::filesystem::exists(meta_path) &&
      std::filesystem::file_size(meta_path)) {
    SPDLOG_INFO("meta file {} exists already.", meta_path);
    proto::KeyInfoMeta meta;
    LoadJsonFileToPbMessage(meta_path, meta);

    if (meta.source_file_size() ==
        std::filesystem::file_size(unique_key_table->Path())) {
      return std::shared_ptr<KeyInfo>(
          new KeyInfo(unique_key_table->Path(), "csv", unique_key_table, meta));
    } else {
      SPDLOG_WARN(
          "unique_key_table file size {} not match meta file size {}, rebuild.",
          std::filesystem::file_size(unique_key_table->Path()),
          meta.source_file_size());
    }
  }

  yacl::crypto::Sha256Hash hash;
  uint32_t lines = 0;
  auto provider = unique_key_table->GetProvider(unique_key_table->Keys());

  auto batch = provider->ReadNextBatch();
  while (!batch.empty()) {
    lines += batch.size();
    for (auto& item : batch) {
      hash.Update(item);
    }
    batch = provider->ReadNextBatch();
  }

  proto::KeyInfoMeta meta;
  auto keys_hash = hash.CumulativeHash();
  meta.mutable_keys_hash()->assign(keys_hash.begin(), keys_hash.end());
  meta.set_duplicate_key_cnt(lines);
  meta.set_unique_key_cnt(lines);
  meta.set_original_cnt(lines);
  meta.set_source_file_size(
      std::filesystem::file_size(unique_key_table->Path()));
  DumpPbMessageToJsonFile(meta, meta_path);

  return std::shared_ptr<KeyInfo>(
      new KeyInfo(unique_key_table->Path(), "csv", unique_key_table, meta));
}

KeyInfo::KeyInfo(std::string path, std::string format,
                 std::shared_ptr<TableWithKeys> sorted_table,
                 proto::KeyInfoMeta meta)
    : Table(std::move(path), std::move(format)),
      table_(std::move(sorted_table)),
      meta_(std::move(meta)) {}

std::shared_ptr<IBasicBatchProvider> KeyInfo::GetKeysProviderWithDupCnt(
    size_t batch_size) const {
  return GetBatchProvider(batch_size);
}

std::shared_ptr<KeysInfoProvider> KeyInfo::GetBatchProvider(
    size_t batch_size) const {
  if (table_->TypeName() == "UniqueKeyTable") {
    return std::make_shared<UniqueTableKeysInfoProvider>(path_, table_->Keys(),
                                                         batch_size);
  } else if (table_->TypeName() == "SortedTable") {
    return std::make_shared<SortedTableKeysInfoProvider>(path_, batch_size);
  } else {
    YACL_THROW("unknow table type: {}", table_->TypeName());
  }
}

std::shared_ptr<arrow::csv::StreamingReader> KeyInfo::GetStreamReader() const {
  return MakeCsvReader(path_, Schema());
}

ResultDumper::ResultDumper(std::string intersect_path, std::string except_path)
    : intersect_path_(std::move(intersect_path)),
      except_path_(std::move(except_path)) {
  if (!intersect_path_.empty()) {
    SPDLOG_INFO("generate intersect part to {}", intersect_path_);
    intersect_file_ = io::GetStdOutFileStream(intersect_path_);
  }
  if (!except_path_.empty()) {
    SPDLOG_INFO("generate except part to {}", except_path_);
    except_file_ = io::GetStdOutFileStream(except_path_);
  }
}

void ResultDumper::ToIntersect(const std::string& line, int64_t duplicate_cnt) {
  Dump(line, duplicate_cnt, intersect_file_, &intersect_cnt_);
}

void ResultDumper::ToExcept(const std::string& line, int64_t duplicate_cnt) {
  Dump(line, duplicate_cnt, except_file_, &except_cnt_);
}

void ResultDumper::Dump(const std::string& line, int64_t duplicate_cnt,
                        std::shared_ptr<std::ofstream>& file,
                        int64_t* total_dump_cnt) {
  *total_dump_cnt += 1 + duplicate_cnt;
  if (file) {
    while (duplicate_cnt-- >= 0) {
      *file << line << '\n';
    }
  }
}

std::vector<std::string> KeyInfo::SourceFileColumns() const {
  return table_->Columns();
}

KeyInfo::StatInfo KeyInfo::ApplyPeerDupCnt(IndexReader& reader,
                                           ResultDumper& dumper) {
  uint32_t self_intersection_count = 0;
  uint32_t peer_intersection_count = 0;
  uint32_t inter_unique_cnt = 0;
  std::string line;
  std::ifstream sorted_file(table_->Path());
  std::getline(sorted_file, line);
  // dump schema line
  dumper.ToIntersect(line);
  dumper.ToExcept(line);

  InterIndexProcessor processor(reader, GetBatchProvider());

  uint32_t sorted_file_index = 0;
  auto inter_index_info = processor.GetBatchInfo();
  SPDLOG_INFO("inter_info: {}", inter_index_info.size());
  while (!inter_index_info.empty()) {
    inter_unique_cnt += inter_index_info.size();
    for (auto& info : inter_index_info) {
      while (sorted_file_index < info.start_index) {
        std::getline(sorted_file, line);
        dumper.ToExcept(line);
        sorted_file_index++;
      }
      for (uint32_t i = 0; i <= info.self_dup_cnt; ++i) {
        std::getline(sorted_file, line);
        dumper.ToIntersect(line, info.peer_dup_cnt);
      }
      sorted_file_index += info.self_dup_cnt + 1;
      self_intersection_count += info.self_dup_cnt + 1;
      peer_intersection_count += info.peer_dup_cnt + 1;
    }

    inter_index_info = processor.GetBatchInfo();
  }

  while (std::getline(sorted_file, line)) {
    dumper.ToExcept(line);
    sorted_file_index++;
  }

  return StatInfo{
      self_intersection_count, peer_intersection_count, sorted_file_index,
      static_cast<uint32_t>(dumper.intersect_cnt() - 1), inter_unique_cnt};
}

InterIndexProcessor::InterIndexProcessor(
    IndexReader& reader, std::shared_ptr<KeysInfoProvider> self_info_provider)
    : reader_(reader), self_info_provider_(self_info_provider) {
  index_with_peer_cnt_ = reader_.GetNextWithPeerCnt();
}

std::vector<InterIndexProcessor::InterInfo>
InterIndexProcessor::GetBatchInfo() {
  if (finish_) {
    return {};
  }

  if (!index_with_peer_cnt_.has_value()) {
    finish_ = true;
    return {};
  }

  std::vector<InterIndexProcessor::InterInfo> inter;
  while (inter.empty()) {
    auto [_, start_index, self_dup_cnt] =
        self_info_provider_->ReadBatchWithInfo();
    if (start_index.empty()) {
      YACL_ENFORCE(!index_with_peer_cnt_.has_value(),
                   "reach end of key_info, but index_with_peer_cnt_ is not "
                   "empty: {}, {}",
                   index_with_peer_cnt_->first, index_with_peer_cnt_->second);
      SPDLOG_INFO("reach end of key_info provider");
      break;
    }
    YACL_ENFORCE(start_index.size() == self_dup_cnt.size(),
                 "start_index size:{} should be equal to self_dup_cnt size:{}",
                 start_index.size(), self_dup_cnt.size());
    for (size_t i = 0; i != start_index.size(); ++i, ++self_info_index_) {
      if (!index_with_peer_cnt_.has_value()) {
        SPDLOG_DEBUG("break: i {} self_info_index_ {}", i, self_info_index_);
        finish_ = true;
        break;
      }
      SPDLOG_DEBUG("i {} self_info_index_ {} key_index {} ", i,
                   self_info_index_, index_with_peer_cnt_->first);
      if (self_info_index_ == index_with_peer_cnt_->first) {
        inter.emplace_back(start_index[i], self_dup_cnt[i],
                           index_with_peer_cnt_->second);
        SPDLOG_DEBUG("start_index {} self_dup_cnt {} peer_dup_cnt {} ",
                     start_index[i], self_dup_cnt[i],
                     index_with_peer_cnt_->second);
        index_with_peer_cnt_ = reader_.GetNextWithPeerCnt();
      } else if (self_info_index_ > index_with_peer_cnt_->first) {
        YACL_THROW_WITH_STACK("duplicated read_cnt in result: {}",
                              index_with_peer_cnt_->first);
      }
    }
  }
  return inter;
}

}  // namespace psi
