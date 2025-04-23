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

#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/base/byte_container_view.h"

#include "psi/utils/batch_provider.h"
#include "psi/utils/io.h"

#include "psi/algorithm/ecdh/ub_psi/ub_psi_cache.pb.h"

namespace psi {

inline constexpr int kMaxCipherSize = 32;

struct UbPsiCacheItem {
  uint32_t origin_index = 0;
  uint32_t shuffle_index = 0;
  uint32_t dup_cnt = 0;
  char data[kMaxCipherSize] = {};
};

class UbPsiCacheProvider : public IBasicBatchProvider,
                           public IShuffledBatchProvider {
 public:
  UbPsiCacheProvider(const std::string& file_path, size_t batch_size);
  ~UbPsiCacheProvider() override {}

  std::vector<std::string> ReadNextBatch() override;

  ShuffledBatch ReadNextShuffledBatch() override;

  std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() override;

  std::vector<std::string> GetSelectedFields();

  std::vector<uint8_t> GetCachePrivateKey();

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  std::vector<UbPsiCacheItem> ReadData(size_t read_count);

  const uint32_t batch_size_;
  std::string file_path_;
  std::ifstream in_;
  proto::UBPsiCacheMeta meta_;
  uint32_t read_count_ = 0;
};

class IUbPsiCache {
 public:
  virtual ~IUbPsiCache() = default;

  virtual void SaveData(yacl::ByteContainerView item, size_t index,
                        size_t shuffle_index) = 0;

  virtual void SaveData(yacl::ByteContainerView item, size_t index,
                        size_t shuffle_index, uint32_t /*dup_cnt*/) {
    SaveData(item, index, shuffle_index);
  }

  virtual void Flush() { return; }
};

class UbPsiCache : public IUbPsiCache {
 public:
  UbPsiCache(const std::string& file_path, uint64_t data_len,
             const std::vector<std::string>& selected_fields,
             std::vector<uint8_t> private_key);

  ~UbPsiCache() {
    try {
      Flush();
      if (out_stream_) {
        out_stream_->Close();
      }
    } catch (const std::exception& e) {
      SPDLOG_ERROR("UbPsiCache flush failed: {}", e.what());
    }
  }

  void SaveData(yacl::ByteContainerView item, size_t index,
                size_t shuffle_index) override {
    SaveData(item, index, shuffle_index, 0);
  }

  void SaveData(yacl::ByteContainerView item, size_t index,
                size_t shuffle_index, uint32_t dup_cnt) override;

  void Flush() override;

 private:
  std::filesystem::path file_path_;
  proto::UBPsiCacheMeta meta_;
  size_t data_len_;
  std::unique_ptr<io::OutputStream> out_stream_;
  size_t cache_cnt_ = 0;
};

}  // namespace psi
