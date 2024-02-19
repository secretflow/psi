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
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yacl/link/link.h"

#include "psi/utils/hash_bucket_cache.h"
#include "psi/utils/index_store.h"

namespace psi {

class IEcPointStore {
 public:
  virtual ~IEcPointStore() = default;

  virtual void Save(std::string ciphertext) = 0;

  virtual void Flush() = 0;

  virtual void Save(const std::vector<std::string>& ciphertext) {
    for (const auto& ct : ciphertext) {
      Save(ct);
    }
  }

  virtual uint64_t ItemCount() = 0;
};

class MemoryEcPointStore : public IEcPointStore {
 public:
  void Save(std::string ciphertext) override;

  std::vector<std::string>& content() { return store_; }

  void Flush() override {}

  uint64_t ItemCount() override { return item_cnt_; }

 private:
  std::vector<std::string> store_;

  uint64_t item_cnt_ = 0;
};

class HashBucketEcPointStore : public IEcPointStore {
 public:
  HashBucketEcPointStore(const std::string& cache_dir, size_t num_bins,
                         bool use_scoped_tmp_dir = true);

  ~HashBucketEcPointStore() override;

  void Save(std::string ciphertext) override;

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

class CachedCsvEcPointStore : public IEcPointStore {
 public:
  CachedCsvEcPointStore(const std::string& path, bool enable_cache,
                        const std::string& party, bool read_only);

  ~CachedCsvEcPointStore() override;

  void Save(std::string ciphertext) override;

  void Save(const std::vector<std::string>& ciphertext) override;

  void Flush() override {
    if (!read_only_) {
      output_stream_->Flush();
    }
  }

  uint64_t ItemCount() override { return item_cnt_; }

  const static std::string cipher_id;

  std::string Path() const { return path_; }

  std::optional<size_t> SearchIndex(const std::string& ciphertext) {
    auto iter = cache_.find(ciphertext);
    if (iter != cache_.end()) {
      return iter->second;
    } else {
      return {};
    }
  }

 protected:
  const std::string path_;

  const bool enable_cache_;

  const std::string party_;

  const bool read_only_;

  std::unique_ptr<io::OutputStream> output_stream_;

  std::unordered_map<std::string, size_t> cache_;

  size_t item_cnt_ = 0;
};

// Get data Indices in csv file
std::vector<uint64_t> GetIndicesByItems(
    const std::string& input_path,
    const std::vector<std::string>& selected_fields,
    const std::vector<std::string>& items, size_t batch_size);

std::vector<uint64_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer);

void FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer,
    IndexWriter* index_writer);

std::pair<std::vector<uint64_t>, std::vector<std::string>>
FinalizeAndComputeIndices(const std::shared_ptr<CachedCsvEcPointStore>& self,
                          const std::shared_ptr<CachedCsvEcPointStore>& peer,
                          size_t batch_size);
}  // namespace psi
