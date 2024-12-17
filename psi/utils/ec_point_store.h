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

#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "psi/utils/batch_provider.h"
#include "psi/utils/hash_bucket_cache.h"
#include "psi/utils/index_store.h"

namespace psi {

class IEcPointStore {
 public:
  virtual ~IEcPointStore() = default;

  virtual void Save(const std::string& ciphertext) { Save(ciphertext, 0); }

  virtual void Save(const std::string& ciphertext, uint32_t duplicate_cnt) = 0;

  virtual void Flush() = 0;

  virtual void Save(const std::vector<std::string>& ciphertext) {
    for (const auto& ct : ciphertext) {
      Save(ct);
    }
  }

  virtual void Save(
      const std::vector<std::string>& ciphertext,
      const std::unordered_map<uint32_t, uint32_t>& duplicate_cnt) {
    for (uint32_t i = 0; i < ciphertext.size(); ++i) {
      auto iter = duplicate_cnt.find(i);
      if (iter != duplicate_cnt.end()) {
        Save(ciphertext[i], iter->second);
      } else {
        Save(ciphertext[i]);
      }
    }
  }

  virtual uint64_t ItemCount() = 0;
};

class MemoryEcPointStore : public IEcPointStore {
 public:
  void Save(const std::string& ciphertext, uint32_t duplicate_cnt) override;

  std::vector<std::string>& content() { return store_; }

  void Flush() override {}

  uint64_t ItemCount() override { return item_cnt_; }

 private:
  std::vector<std::string> store_;
  std::unordered_map<uint32_t, uint32_t> item_extra_dup_cnt_map_;

  uint64_t item_cnt_ = 0;
};

class HashBucketEcPointStore : public IEcPointStore {
 public:
  HashBucketEcPointStore(const std::string& cache_dir, size_t num_bins,
                         bool use_scoped_tmp_dir = true);

  ~HashBucketEcPointStore() override;

  void Save(const std::string& ciphertext, uint32_t duplicate_cnt) override;

  [[nodiscard]] size_t num_bins() const { return num_bins_; }

  void Flush() override { cache_->Flush(); }

  uint64_t ItemCount() override;

  std::vector<HashBucketCache::BucketItem> LoadBucketItems(size_t bin_idx) {
    return cache_->LoadBucketItems(bin_idx);
  };

 protected:
  std::unique_ptr<HashBucketCache> cache_;

  const size_t num_bins_;
};

class UbPsiClientCacheFileStore : public IEcPointStore {
 public:
  inline static constexpr size_t kMaxCipherSize = 32;
  struct CacheItem {
    char ciphertext[kMaxCipherSize];
    uint32_t duplicate_cnt;
  };
  struct CacheMeta {
    uint32_t item_cnt;
    uint32_t peer_cnt;
    uint32_t cipher_len;
  };

 public:
  explicit UbPsiClientCacheFileStore(std::string path, size_t cipher_len);

  ~UbPsiClientCacheFileStore() override;

  void Save(const std::string& ciphertext, uint32_t duplicate_cnt) override;

  void Flush() override;

  uint64_t ItemCount() override { return item_cnt_; }
  uint64_t PeerCount() const { return peer_cnt_; }

  std::shared_ptr<IBasicBatchProvider> GetBatchProvider(
      size_t batch_size) const;

  std::string Path() const { return path_; }

 protected:
  void LoadMeta();
  void DumpMeta();

  std::string path_;
  std::string meta_path_;

  std::fstream output_stream_;
  uint32_t cipher_len_ = 0;
  uint32_t item_cnt_ = 0;
  uint32_t peer_cnt_ = 0;
  CacheMeta meta_;
};

class UbPsiClientCacheMemoryStore : public IEcPointStore {
 public:
  struct CacheIndex {
    uint32_t index;
    uint32_t duplicate_cnt;
  };

  explicit UbPsiClientCacheMemoryStore();

  ~UbPsiClientCacheMemoryStore() override;

  void Save(const std::string& ciphertext, uint32_t duplicate_cnt) override;

  uint64_t ItemCount() override { return item_cnt_; }

  std::optional<CacheIndex> Find(const std::string& ciphertext) const;

  void Flush() override {}

 protected:
  std::unordered_map<std::string, CacheIndex> cache_;
  uint32_t item_cnt_ = 0;
};

// Get data Indices in csv file
std::vector<uint64_t> GetIndicesByItems(
    const std::string& input_path,
    const std::vector<std::string>& selected_fields,
    const std::vector<std::string>& items, size_t batch_size);

std::vector<uint64_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer);

std::pair<uint32_t, uint32_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer,
    IndexWriter* index_writer);

struct IntersectionIndexInfo {
  std::vector<uint32_t> self_indices;
  std::vector<uint32_t> peer_indices;
  std::vector<uint32_t> self_dup_cnt;
  std::vector<uint32_t> peer_dup_cnt;
};

IntersectionIndexInfo ComputeIndicesWithDupCnt(
    const std::shared_ptr<UbPsiClientCacheMemoryStore>& self,
    const std::shared_ptr<UbPsiClientCacheFileStore>& peer, size_t batch_size);

}  // namespace psi
