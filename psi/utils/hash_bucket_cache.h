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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "fmt/format.h"
#include "yacl/base/exception.h"

#include "psi/utils/io.h"
#include "psi/utils/multiplex_disk_cache.h"

namespace psi {

class HashBucketCache {
 public:
  struct BucketItem {
    uint64_t index;
    std::string base64_data;

    std::string Serialize() { return fmt::format("{},{}", index, base64_data); }

    static BucketItem Deserialize(std::string_view data_str) {
      BucketItem item;
      std::vector<absl::string_view> tokens = absl::StrSplit(data_str, ',');
      YACL_ENFORCE(tokens.size() == 2, "should have two tokens, actual: {}",
                   tokens.size());
      YACL_ENFORCE(absl::SimpleAtoi(tokens[0], &item.index),
                   "cannot convert {} to idx",
                   std::string(tokens[0].data(), tokens[0].size()));
      item.base64_data = std::string(tokens[1].data(), tokens[1].size());

      return item;
    }
  };

  HashBucketCache(std::string target_dir, uint32_t bucket_num,
                  bool use_scoped_tmp_dir = true);

  ~HashBucketCache();

  void WriteItem(const std::string& data);

  void Flush();

  std::vector<BucketItem> LoadBucketItems(uint32_t index);

  uint32_t BucketNum() const { return bucket_num_; }

  uint64_t ItemCount() const { return item_index_; }

 private:
  std::unique_ptr<MultiplexDiskCache> disk_cache_;

  std::vector<std::unique_ptr<io::OutputStream>> bucket_os_vec_;

  uint32_t bucket_num_;

  uint64_t item_index_;
};

std::unique_ptr<HashBucketCache> CreateCacheFromCsv(
    const std::string& csv_path, const std::vector<std::string>& schema_names,
    const std::string& cache_dir, uint32_t bucket_num,
    uint32_t read_batch_size = 4096, bool use_scoped_tmp_dir = true);

}  // namespace psi
