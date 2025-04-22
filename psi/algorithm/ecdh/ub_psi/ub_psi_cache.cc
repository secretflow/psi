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

#include "psi/algorithm/ecdh/ub_psi/ub_psi_cache.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/utils/batch_provider.h"
#include "psi/utils/pb_helper.h"
#include "psi/utils/serialize.h"

namespace psi {

namespace {

std::filesystem::path GetUbPsiCacheFileName(const std::string& cache_path) {
  return std::filesystem::path(cache_path) / "ub_psi_cache.bin";
}

std::filesystem::path GetUbPsiCacheMetaName(const std::string& cache_path) {
  return std::filesystem::path(cache_path) / "ub_psi_cache.meta";
}

}  // namespace

UbPsiCacheProvider::UbPsiCacheProvider(const std::string& file_path,
                                       size_t batch_size)
    : batch_size_(batch_size), file_path_(file_path) {
  auto data_file = GetUbPsiCacheFileName(file_path_);
  YACL_ENFORCE(std::filesystem::exists(data_file), "{} not exists",
               data_file.string());
  in_ = std::ifstream(data_file, std::ios::binary);

  auto meta_file = GetUbPsiCacheMetaName(file_path_);
  YACL_ENFORCE(std::filesystem::exists(meta_file), "{} not exists",
               meta_file.string());

  LoadJsonFileToPbMessage(meta_file, meta_);

  auto file_item_cnt =
      std::filesystem::file_size(data_file) / sizeof(UbPsiCacheItem);
  YACL_ENFORCE(file_item_cnt == meta_.item_count(),
               "file item count {}  mismatch meta record {}", file_item_cnt,
               meta_.item_count());
}

std::vector<UbPsiCacheItem> UbPsiCacheProvider::ReadData(size_t read_count) {
  std::vector<UbPsiCacheItem> ret(read_count);

  in_.read(reinterpret_cast<char*>(ret.data()),
           read_count * sizeof(UbPsiCacheItem));

  return ret;
}

std::vector<std::string> UbPsiCacheProvider::ReadNextBatch() {
  auto shuffled_batch = ReadNextShuffledBatch();
  return shuffled_batch.batch_items;
}

std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
UbPsiCacheProvider::ReadNextBatchWithDupCnt() {
  auto shuffled_batch = ReadNextShuffledBatch();
  std::unordered_map<uint32_t, uint32_t> dup_cnt;
  for (uint32_t i = 0; i < shuffled_batch.dup_cnts.size(); ++i) {
    if (shuffled_batch.dup_cnts[i] != 0) {
      dup_cnt[i] = shuffled_batch.dup_cnts[i];
    }
  }
  return {shuffled_batch.batch_items, dup_cnt};
}

IShuffledBatchProvider::ShuffledBatch
UbPsiCacheProvider::ReadNextShuffledBatch() {
  ShuffledBatch shuffled_batch;

  if (read_count_ >= meta_.item_count()) {
    return shuffled_batch;
  }

  size_t count = std::min(batch_size_, meta_.item_count() - read_count_);

  if (count > 0) {
    auto items = ReadData(count);
    for (const auto& item : items) {
      shuffled_batch.batch_items.emplace_back(item.data, meta_.item_len());
      shuffled_batch.batch_indices.push_back(item.origin_index);
      shuffled_batch.shuffled_indices.push_back(item.shuffle_index);
      shuffled_batch.dup_cnts.push_back(item.dup_cnt);
    }
    read_count_ += count;
  }

  return shuffled_batch;
}

std::vector<std::string> UbPsiCacheProvider::GetSelectedFields() {
  return std::vector<std::string>(meta_.key_cols().begin(),
                                  meta_.key_cols().end());
}
std::vector<uint8_t> UbPsiCacheProvider::GetCachePrivateKey() {
  return std::vector<uint8_t>(meta_.priv_key().begin(), meta_.priv_key().end());
}

UbPsiCache::UbPsiCache(const std::string& file_path, uint64_t data_len,
                       const std::vector<std::string>& selected_fields,
                       std::vector<uint8_t> private_key)
    : file_path_(file_path), data_len_(data_len) {
  YACL_ENFORCE(data_len_ < kMaxCipherSize, "data_len:{} too large", data_len_);

  meta_.set_item_len(data_len_);
  meta_.set_version("0.0.1");
  meta_.mutable_priv_key()->assign(private_key.begin(), private_key.end());
  meta_.mutable_key_cols()->Assign(selected_fields.begin(),
                                   selected_fields.end());

  out_stream_ = io::BuildOutputStream(
      io::FileIoOptions(GetUbPsiCacheFileName(file_path)));
}

void UbPsiCache::Flush() {
  meta_.set_item_count(cache_cnt_);
  DumpPbMessageToJsonFile(meta_, GetUbPsiCacheMetaName(file_path_));
  out_stream_->Flush();
}

void UbPsiCache::SaveData(yacl::ByteContainerView item, size_t index,
                          size_t shuffle_index, uint32_t dup_cnt) {
  YACL_ENFORCE(item.size() == data_len_, "item size:{} data_len_:{}",
               item.size(), data_len_);

  UbPsiCacheItem cache_item{
      .origin_index = static_cast<uint32_t>(index),
      .shuffle_index = static_cast<uint32_t>(shuffle_index),
      .dup_cnt = dup_cnt};
  std::memcpy(&cache_item.data[0], item.data(), data_len_);
  out_stream_->Write(&cache_item, sizeof(UbPsiCacheItem));
  SPDLOG_DEBUG("Save Cache: origin_index:{} shuffle_index:{} dup_cnt:{}",
               cache_item.origin_index, cache_item.shuffle_index,
               cache_item.dup_cnt);
  ++cache_cnt_;
}

}  // namespace psi
