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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "batch_provider.h"
#include "fmt/format.h"
#include "yacl/base/exception.h"
#include "yacl/base/int128.h"

#include "psi/utils/io.h"
#include "psi/utils/multiplex_disk_cache.h"

namespace psi {

class HashBucketCache {
 public:
  struct BucketItem {
    uint128_t sec_hash = 0;
    uint64_t index = 0;
    uint32_t extra_dup_cnt = 0;
    uint32_t pad = 0;
    std::string base64_data;

    std::string Serialize() {
      return fmt::format("{},{},{}", index, extra_dup_cnt, base64_data);
    }

    static size_t hash(const BucketItem& item) {
      return std::hash<std::string>()(item.base64_data);
    }

    bool operator==(const BucketItem& other) const {
      return base64_data == other.base64_data;
    }

    static BucketItem Deserialize(std::string_view data_str) {
      BucketItem item;
      std::vector<absl::string_view> tokens = absl::StrSplit(data_str, ',');
      YACL_ENFORCE(tokens.size() == 3, "should have three tokens, actual: {}",
                   tokens.size());
      YACL_ENFORCE(absl::SimpleAtoi(tokens[0], &item.index),
                   "cannot convert {} to idx",
                   std::string(tokens[0].data(), tokens[0].size()));
      YACL_ENFORCE(absl::SimpleAtoi(tokens[1], &item.extra_dup_cnt),
                   "cannot convert {} to duplicate_cnt",
                   std::string(tokens[1].data(), tokens[1].size()));
      item.base64_data = std::string(tokens[2].data(), tokens[2].size());

      return item;
    }
  };

  struct HashBucketIter {
    size_t operator()(const BucketItem& item) const {
      return std::hash<std::string>()(item.base64_data);
    }
  };

  HashBucketCache(const std::string& target_dir, uint32_t bucket_num,
                  bool use_scoped_tmp_dir = true);

  ~HashBucketCache();

  void WriteItem(const std::string& data, uint32_t duplicate_cnt = 0);

  void Flush();

  std::vector<BucketItem> LoadBucketItems(uint32_t index);

  size_t GetBucketSize(uint32_t index);

  uint32_t BucketNum() const { return bucket_num_; }

  uint64_t ItemCount() const { return item_index_; }

 private:
  std::unique_ptr<MultiplexDiskCache> disk_cache_;

  std::vector<std::unique_ptr<io::OutputStream>> bucket_os_vec_;

  std::vector<size_t> bucket_data_sizes_;

  uint32_t bucket_num_;

  uint64_t item_index_;
};

std::unique_ptr<HashBucketCache> CreateCacheFromCsv(
    const std::string& csv_path, const std::vector<std::string>& schema_names,
    const std::string& cache_dir, uint32_t bucket_num,
    uint32_t read_batch_size = 4096, bool use_scoped_tmp_dir = true);

std::unique_ptr<HashBucketCache> CreateCacheFromProvider(
    std::shared_ptr<IBasicBatchProvider> provider, const std::string& cache_dir,
    uint32_t bucket_num, bool use_scoped_tmp_dir = true);

}  // namespace psi
