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

#include <filesystem>
#include <memory>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"

#include "psi/utils/arrow_csv_batch_provider.h"

namespace psi {

HashBucketCache::HashBucketCache(std::string target_dir, uint32_t bucket_num,
                                 bool use_scoped_tmp_dir)
    : bucket_num_(bucket_num), item_index_(0) {
  YACL_ENFORCE(bucket_num_ > 0);
  disk_cache_ = std::make_unique<MultiplexDiskCache>(
      std::filesystem::path(target_dir), use_scoped_tmp_dir);
  YACL_ENFORCE(disk_cache_, "cannot create disk cache from dir={}", target_dir);
  disk_cache_->CreateOutputStreams(bucket_num_, &bucket_os_vec_);
}

HashBucketCache::~HashBucketCache() {
  bucket_os_vec_.clear();
  disk_cache_ = nullptr;
}

void HashBucketCache::WriteItem(const std::string& data) {
  BucketItem bucket_item;
  bucket_item.index = item_index_;
  bucket_item.base64_data = absl::Base64Escape(data);

  auto& out = bucket_os_vec_[std::hash<std::string>()(bucket_item.base64_data) %
                             bucket_os_vec_.size()];
  out->Write(bucket_item.Serialize());
  out->Write("\n");
  item_index_++;
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

std::unique_ptr<HashBucketCache> CreateCacheFromCsv(
    const std::string& csv_path, const std::vector<std::string>& schema_names,
    const std::string& cache_dir, uint32_t bucket_num, uint32_t read_batch_size,
    bool use_scoped_tmp_dir) {
  auto bucket_cache = std::make_unique<HashBucketCache>(cache_dir, bucket_num,
                                                        use_scoped_tmp_dir);

  auto batch_provider = std::make_unique<ArrowCsvBatchProvider>(
      csv_path, schema_names, read_batch_size);
  while (true) {
    auto items = batch_provider->ReadNextBatch();
    if (items.empty()) {
      break;
    }
    for (const auto& it : items) {
      bucket_cache->WriteItem(it);
    }
  }
  bucket_cache->Flush();

  return bucket_cache;
}

}  // namespace psi
