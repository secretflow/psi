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

#include "psi/utils/hash_bucket_cache.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"

#include "psi/utils/arrow_csv_batch_provider.h"

namespace psi {

HashBucketCache::HashBucketCache(const std::string& target_dir,
                                 uint32_t bucket_num, bool use_scoped_tmp_dir)
    : bucket_num_(bucket_num), item_index_(0) {
  YACL_ENFORCE(bucket_num_ > 0);
  if (!std::filesystem::exists(target_dir)) {
    SPDLOG_INFO("target dir={} does not exists, create it", target_dir);
    std::filesystem::create_directories(target_dir);
  }
  disk_cache_ = std::make_unique<MultiplexDiskCache>(
      std::filesystem::path(target_dir), use_scoped_tmp_dir);
  YACL_ENFORCE(disk_cache_, "cannot create disk cache from dir={}", target_dir);
  disk_cache_->CreateOutputStreams(bucket_num_, &bucket_os_vec_);
  bucket_data_sizes_ = std::vector<size_t>(bucket_num, 0);
}

HashBucketCache::~HashBucketCache() {
  bucket_os_vec_.clear();
  disk_cache_ = nullptr;
}

void HashBucketCache::WriteItem(const std::string& data,
                                uint32_t duplicate_cnt) {
  BucketItem bucket_item;
  bucket_item.index = item_index_;
  bucket_item.extra_dup_cnt = duplicate_cnt;
  bucket_item.base64_data = absl::Base64Escape(data);
  size_t bucket_idx =
      std::hash<std::string>()(bucket_item.base64_data) % bucket_os_vec_.size();
  auto& out = bucket_os_vec_[bucket_idx];
  out->Write(bucket_item.Serialize());
  out->Write("\n");
  item_index_++;
  bucket_data_sizes_[bucket_idx]++;
}

void HashBucketCache::Flush() {
  // Flush files.
  for (const auto& out : bucket_os_vec_) {
    out->Flush();
  }
}

std::vector<HashBucketCache::BucketItem> HashBucketCache::LoadBucketItems(
    uint32_t index) {
  std::vector<BucketItem> ret;
  auto in = disk_cache_->CreateInputStream(index);

  std::string line;
  while (in->GetLine(&line)) {
    auto item = BucketItem::Deserialize(line);
    ret.push_back(std::move(item));
  }
  return ret;
}

size_t HashBucketCache::GetBucketSize(uint32_t index) {
  return bucket_data_sizes_[index];
}

std::unique_ptr<HashBucketCache> CreateCacheFromCsv(
    const std::string& csv_path, const std::vector<std::string>& schema_names,
    const std::string& cache_dir, uint32_t bucket_num, uint32_t read_batch_size,
    bool use_scoped_tmp_dir) {
  auto bucket_cache = std::make_unique<HashBucketCache>(cache_dir, bucket_num,
                                                        use_scoped_tmp_dir);

  std::shared_ptr<IBasicBatchProvider> batch_provider =
      std::make_unique<ArrowCsvBatchProvider>(csv_path, schema_names,
                                              read_batch_size);

  return CreateCacheFromProvider(batch_provider, cache_dir, bucket_num,
                                 use_scoped_tmp_dir);
}

std::unique_ptr<HashBucketCache> CreateCacheFromProvider(
    std::shared_ptr<IBasicBatchProvider> provider, const std::string& cache_dir,
    uint32_t bucket_num, bool use_scoped_tmp_dir) {
  auto bucket_cache = std::make_unique<HashBucketCache>(cache_dir, bucket_num,
                                                        use_scoped_tmp_dir);

  while (true) {
    auto [items, duplicate_cnt] = provider->ReadNextBatchWithDupCnt();
    if (items.empty()) {
      break;
    }
    for (size_t i = 0; i < items.size(); ++i) {
      bucket_cache->WriteItem(items[i], duplicate_cnt[i]);
    }
    bucket_cache->Flush();
  }
  return bucket_cache;
}

}  // namespace psi
