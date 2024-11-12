// Copyright 2022 Ant Group Co., Ltd.
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

#include "psi/utils/ec_point_store.h"

#include <omp.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "absl/strings/escaping.h"
#include "batch_provider.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

#include "psi/utils/arrow_csv_batch_provider.h"

namespace psi {

void MemoryEcPointStore::Save(const std::string& ciphertext,
                              uint32_t duplicate_cnt) {
  if (duplicate_cnt > 0) {
    item_extra_dup_cnt_map_[store_.size()] = duplicate_cnt;
  }
  store_.push_back(std::move(ciphertext));
  item_cnt_++;
}

HashBucketEcPointStore::HashBucketEcPointStore(const std::string& cache_dir,
                                               size_t num_bins,
                                               bool use_scoped_tmp_dir)
    : num_bins_(num_bins) {
  cache_ = std::make_unique<HashBucketCache>(cache_dir, num_bins,
                                             use_scoped_tmp_dir);
}

void HashBucketEcPointStore::Save(const std::string& ciphertext,
                                  uint32_t duplicate_cnt) {
  cache_->WriteItem(ciphertext, duplicate_cnt);
}

uint64_t HashBucketEcPointStore::ItemCount() { return cache_->ItemCount(); }

HashBucketEcPointStore::~HashBucketEcPointStore() { Flush(); }

std::vector<uint64_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer) {
  YACL_ENFORCE_EQ(self->num_bins(), peer->num_bins());
  self->Flush();
  peer->Flush();

  // Compute indices
  std::vector<uint64_t> indices;
  for (size_t bin_idx = 0; bin_idx < self->num_bins(); ++bin_idx) {
    std::vector<HashBucketCache::BucketItem> self_results =
        self->LoadBucketItems(bin_idx);
    std::vector<HashBucketCache::BucketItem> peer_results =
        peer->LoadBucketItems(bin_idx);
    std::unordered_set<std::string> peer_set;
    peer_set.reserve(peer_results.size());
    std::for_each(peer_results.begin(), peer_results.end(),
                  [&](HashBucketCache::BucketItem& item) {
                    peer_set.insert(std::move(item.base64_data));
                  });
    for (const auto& item : self_results) {
      if (peer_set.find(item.base64_data) != peer_set.end()) {
        indices.push_back(item.index);
      }
    }
  }
  // Sort to make `FilterFileByIndices` happy.
  std::sort(indices.begin(), indices.end());
  return indices;
}

std::pair<uint32_t, uint32_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer,
    IndexWriter* index_writer) {
  YACL_ENFORCE_EQ(self->num_bins(), peer->num_bins());
  self->Flush();
  peer->Flush();
  uint32_t peer_inter_cnt = 0;
  uint32_t peer_total_cnt = 0;

  // Compute indices
  for (size_t bin_idx = 0; bin_idx < self->num_bins(); ++bin_idx) {
    std::vector<HashBucketCache::BucketItem> self_results =
        self->LoadBucketItems(bin_idx);
    std::vector<HashBucketCache::BucketItem> peer_results =
        peer->LoadBucketItems(bin_idx);
    std::for_each(peer_results.begin(), peer_results.end(), [&](auto& item) {
      peer_total_cnt += item.extra_dup_cnt + 1;
    });
    std::unordered_set<HashBucketCache::BucketItem,
                       HashBucketCache::HashBucketIter>
        peer_set(peer_results.begin(), peer_results.end());
    for (const auto& item : self_results) {
      auto peer = peer_set.find(item);
      if (peer != peer_set.end()) {
        index_writer->WriteCache(item.index, peer->extra_dup_cnt);
        peer_inter_cnt += peer->extra_dup_cnt + 1;
      }
    }

    index_writer->Commit();
  }
  return {peer_total_cnt, peer_inter_cnt};
}

IntersectionIndexInfo ComputeIndicesWithDupCnt(
    const std::shared_ptr<UbPsiClientCacheMemoryStore>& self,
    const std::shared_ptr<UbPsiClientCacheFileStore>& peer, size_t batch_size) {
  self->Flush();
  peer->Flush();

  SPDLOG_INFO("Begin ComputeIndices");

  IntersectionIndexInfo index_info;

  size_t batch_count = 0;
  size_t processed_cnt = 0;

  auto peer_provider = peer->GetBatchProvider(batch_size);

  size_t compare_thread_num = std::thread::hardware_concurrency();
  std::vector<std::vector<size_t>> self_indices(compare_thread_num);
  std::vector<std::vector<size_t>> peer_indices(compare_thread_num);
  std::vector<std::vector<size_t>> self_dup_cnt(compare_thread_num);
  std::vector<std::vector<size_t>> peer_dup_cnt(compare_thread_num);

  while (true) {
    SPDLOG_INFO("begin read compare batch {}", batch_count);
    auto [peer_items, peer_cnt] = peer_provider->ReadNextBatchWithDupCnt();
    SPDLOG_INFO("end read compare batch {}, items: {}", batch_count,
                peer_items.size());

    if (peer_items.empty()) {
      break;
    }

    size_t compare_size =
        (peer_items.size() + compare_thread_num - 1) / compare_thread_num;

    auto compare_proc = [&](int idx, const auto& peer_items,
                            const auto& peer_cnt) -> void {
      uint64_t begin = idx * compare_size;
      uint64_t end =
          std::min<uint64_t>(peer_items.size(), begin + compare_size);

      for (size_t i = begin; i < end; i++) {
        auto search_ret = self->Find(peer_items[i]);
        if (search_ret.has_value()) {
          self_indices[idx].push_back(search_ret.value().index);
          peer_indices[idx].push_back(processed_cnt + i);
          self_dup_cnt[idx].push_back(search_ret.value().duplicate_cnt);
          auto iter = peer_cnt.find(i);
          if (iter != peer_cnt.end()) {
            peer_dup_cnt[idx].push_back(iter->second);
          } else {
            peer_dup_cnt[idx].push_back(0);
          }
        }
      }
    };

    std::vector<std::future<void>> f_compare(compare_thread_num);
    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i] = std::async(compare_proc, i, peer_items, peer_cnt);
    }

    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i].get();
    }

    batch_count++;
    processed_cnt += peer_items.size();

    SPDLOG_INFO("ComputeIndices, batch_count:{}, item_cnt: {}", batch_count,
                processed_cnt);
  }

  for (size_t i = 0; i < compare_thread_num; i++) {
    index_info.self_indices.insert(index_info.self_indices.end(),
                                   self_indices[i].begin(),
                                   self_indices[i].end());
    index_info.peer_indices.insert(index_info.peer_indices.end(),
                                   peer_indices[i].begin(),
                                   peer_indices[i].end());
    index_info.self_dup_cnt.insert(index_info.self_dup_cnt.end(),
                                   self_dup_cnt[i].begin(),
                                   self_dup_cnt[i].end());
    index_info.peer_dup_cnt.insert(index_info.peer_dup_cnt.end(),
                                   peer_dup_cnt[i].begin(),
                                   peer_dup_cnt[i].end());
  }

  SPDLOG_INFO("End FinalizeAndComputeIndices, batch_count:{}", batch_count);
  return index_info;
}

std::vector<uint64_t> GetIndicesByItems(
    const std::string& input_path,
    const std::vector<std::string>& selected_fields,
    const std::vector<std::string>& items, size_t batch_size) {
  std::vector<uint64_t> indices;

  std::unordered_set<std::string> items_set;

  items_set.insert(items.begin(), items.end());

  auto batch_provider = std::make_shared<ArrowCsvBatchProvider>(
      input_path, selected_fields, batch_size);

  size_t compare_thread_num = omp_get_num_procs();

  size_t item_index = 0;
  size_t batch_count = 0;
  while (true) {
    auto batch_items = batch_provider->ReadNextBatch();
    if (batch_items.empty()) {
      break;
    }

    size_t compare_size =
        (batch_items.size() + compare_thread_num - 1) / compare_thread_num;

    std::vector<std::vector<uint64_t>> result(compare_thread_num);

    auto compare_proc = [&](int idx) -> void {
      uint64_t begin = idx * compare_size;
      uint64_t end = std::min<size_t>(batch_items.size(), begin + compare_size);

      for (uint64_t i = begin; i < end; i++) {
        auto search_ret = items_set.find(batch_items[i]);
        if (search_ret != items_set.end()) {
          result[idx].push_back(item_index + i);
        }
      }
    };

    std::vector<std::future<void>> f_compare(compare_thread_num);
    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i] = std::async(compare_proc, i);
    }

    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i].get();
    }

    for (const auto& r : result) {
      indices.insert(indices.end(), r.begin(), r.end());
    }

    batch_count++;

    item_index += batch_items.size();
    SPDLOG_INFO("GetIndices batch count:{}, item_index:{}", batch_count,
                item_index);
  }
  SPDLOG_INFO("Finish GetIndices, indices size:{}", indices.size());

  return indices;
}

namespace {

class UbPsiClientCacheFileStoreProvider : public IBasicBatchProvider {
 public:
  UbPsiClientCacheFileStoreProvider(std::string path, size_t batch_size,
                                    uint32_t cipher_len)
      : batch_size_(batch_size), cipher_len_(cipher_len) {
    YACL_ENFORCE(std::filesystem::exists(path),
                 "Ub psi cache path:{} not exists", path);
    input_stream_ = std::ifstream(path, std::ios::binary);
    item_cnt_ = std::filesystem::file_size(path) /
                sizeof(UbPsiClientCacheFileStore::CacheItem);
    buffer_ = ReadFutureBatch();
  }

  ~UbPsiClientCacheFileStoreProvider() override {
    if (buffer_.valid()) {
      buffer_.get();
    }
    if (input_stream_.is_open()) {
      input_stream_.close();
    }
  }

  std::vector<std::string> ReadNextBatch() override {
    return ReadNextBatchWithDupCnt().first;
  }

  std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() override {
    auto cache_item = buffer_.get();
    if (cache_item.empty()) {
      return std::make_pair(std::vector<std::string>(),
                            std::unordered_map<uint32_t, uint32_t>());
    } else {
      buffer_ = ReadFutureBatch();
    }

    std::vector<std::string> items;
    items.reserve(cache_item.size());
    std::unordered_map<uint32_t, uint32_t> dup_cnt;
    for (size_t i = 0; i < cache_item.size(); i++) {
      auto& item = cache_item[i];
      items.emplace_back(item.ciphertext, cipher_len_);
      if (item.duplicate_cnt > 0) {
        dup_cnt[i] = item.duplicate_cnt;
      }
    }
    return std::make_pair(std::move(items), std::move(dup_cnt));
  }

  size_t GetItemCount() const { return item_cnt_; }

  [[nodiscard]] size_t batch_size() const override { return batch_size_; };

 private:
  std::future<std::vector<UbPsiClientCacheFileStore::CacheItem>>
  ReadFutureBatch() {
    return std::async(std::launch::async, [this]() {
      std::vector<UbPsiClientCacheFileStore::CacheItem> cache_item;
      if (read_cnt_ >= item_cnt_) {
        return cache_item;
      }
      auto count = std::min<size_t>(batch_size_, item_cnt_ - read_cnt_);
      cache_item.resize(count);
      input_stream_.read(reinterpret_cast<char*>(cache_item.data()),
                         count * sizeof(UbPsiClientCacheFileStore::CacheItem));
      read_cnt_ += count;
      return cache_item;
    });
  }

  std::ifstream input_stream_;
  size_t batch_size_ = 0;
  uint32_t cipher_len_ = 0;

  size_t item_cnt_ = 0;
  size_t read_cnt_ = 0;
  std::future<std::vector<UbPsiClientCacheFileStore::CacheItem>> buffer_;
};

}  // namespace

void UbPsiClientCacheFileStore::LoadMeta() {
  std::ifstream meta_stream(meta_path_, std::ios::binary);
  meta_stream.read(reinterpret_cast<char*>(&meta_), sizeof(CacheMeta));
}

void UbPsiClientCacheFileStore::DumpMeta() {
  meta_ = CacheMeta{
      .item_cnt = item_cnt_, .peer_cnt = peer_cnt_, .cipher_len = cipher_len_};
  std::ofstream meta_stream(meta_path_, std::ios::binary);
  meta_stream.write(reinterpret_cast<const char*>(&meta_), sizeof(CacheMeta));
}

UbPsiClientCacheFileStore::UbPsiClientCacheFileStore(std::string path,
                                                     size_t cipher_len)
    : path_(std::move(path)),
      meta_path_(path_ + ".meta"),
      cipher_len_(cipher_len) {
  YACL_ENFORCE(cipher_len_ <= kMaxCipherSize, "cipher_len:{} > max:{}",
               cipher_len_, kMaxCipherSize);
  auto file_path = std::filesystem::path(path_);

  if (!std::filesystem::exists(file_path.parent_path())) {
    SPDLOG_INFO("create directory:{}", file_path.parent_path().string());
    std::filesystem::create_directories(file_path.parent_path());
  }

  output_stream_ = std::fstream(path_, std::ios::app | std::ios::binary);
  item_cnt_ = std::filesystem::file_size(path_) /
              sizeof(UbPsiClientCacheFileStore::CacheItem);

  if (std::filesystem::exists(meta_path_)) {
    LoadMeta();
    YACL_ENFORCE(cipher_len_ == meta_.cipher_len,
                 "cipher_len not match,  {} != {} in meta", cipher_len_,
                 meta_.cipher_len);
    YACL_ENFORCE(item_cnt_ == meta_.item_cnt,
                 "item_cnt not match, meta {} != {} in meta", item_cnt_,
                 meta_.item_cnt);
    peer_cnt_ = meta_.peer_cnt;
  } else {
    DumpMeta();
  }
}

UbPsiClientCacheFileStore::~UbPsiClientCacheFileStore() { Flush(); }

std::shared_ptr<IBasicBatchProvider>
UbPsiClientCacheFileStore::GetBatchProvider(size_t batch_size) const {
  return std::make_shared<UbPsiClientCacheFileStoreProvider>(path_, batch_size,
                                                             cipher_len_);
}

void UbPsiClientCacheFileStore::Flush() {
  output_stream_.flush();
  DumpMeta();
}

void UbPsiClientCacheFileStore::Save(const std::string& ciphertext,
                                     uint32_t duplicate_cnt) {
  YACL_ENFORCE(ciphertext.size() == cipher_len_,
               "ciphertext size:{} != cipher_len:{}", ciphertext.size(),
               cipher_len_);
  CacheItem item;
  memcpy(item.ciphertext, ciphertext.data(), ciphertext.size());
  item.duplicate_cnt = duplicate_cnt;
  output_stream_.write(reinterpret_cast<char*>(&item), sizeof(CacheItem));
  item_cnt_++;
  peer_cnt_ += duplicate_cnt + 1;
}

UbPsiClientCacheMemoryStore::UbPsiClientCacheMemoryStore() = default;

UbPsiClientCacheMemoryStore::~UbPsiClientCacheMemoryStore() {}

void UbPsiClientCacheMemoryStore::Save(const std::string& ciphertext,
                                       uint32_t duplicate_cnt) {
  cache_[ciphertext] =
      CacheIndex{.index = item_cnt_, .duplicate_cnt = duplicate_cnt};
  item_cnt_++;
}

std::optional<UbPsiClientCacheMemoryStore::CacheIndex>
UbPsiClientCacheMemoryStore::Find(const std::string& ciphertext) const {
  auto iter = cache_.find(ciphertext);
  if (iter != cache_.end()) {
    return iter->second;
  } else {
    return std::nullopt;
  }
}

}  // namespace psi
