// Copyright 2025
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
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

#include "psi/utils/batch_provider.h"
#include "psi/utils/io.h"

namespace psi::ecdh {

// This struct doesn't need to record data
struct ThresholdEcdhPsiCacheItem {
  uint32_t origin_index = 0;
  uint32_t shuffle_index = 0;
  uint32_t dup_cnt = 0;
};

// This class is used for sender to read `ThresholdEcdhPsiCacheItem` and restore
// receiver's index.
class ThresholdEcdhPsiCacheProvider : public IShuffledBatchProvider {
 public:
  ThresholdEcdhPsiCacheProvider(const std::filesystem::path& file_path,
                                size_t batch_size);
  ~ThresholdEcdhPsiCacheProvider() override {}

  ShuffledBatch ReadNextShuffledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  void LoadMeta();

  std::vector<ThresholdEcdhPsiCacheItem> ReadData(size_t read_count);

  const uint32_t batch_size_;
  std::filesystem::path meta_path_;
  std::ifstream in_;
  uint32_t cache_cnt_ = 0;
  uint32_t read_cnt_ = 0;
};

// This class is used for sender to store index, shuffled index and duplicate
// count of receiver. `ThresholdEcdhPsiCacheItem` will help sender restore
// receiver's index in later steps. The implementation refers to `UbPsiCache`.
class ThresholdEcdhPsiCache {
 public:
  ThresholdEcdhPsiCache(const std::filesystem::path& file_path);

  ~ThresholdEcdhPsiCache() {
    try {
      Flush();
      if (out_stream_) {
        out_stream_->Close();
      }
    } catch (const std::exception& e) {
      SPDLOG_ERROR("ThresholdEcdhPsiCache flush failed: {}", e.what());
    }
  }

  void SaveData(size_t index, size_t shuffle_index) {
    SaveData(index, shuffle_index, 0);
  }

  void SaveData(size_t index, size_t shuffle_index, uint32_t dup_cnt);

  void Flush();

 private:
  void DumpMeta();

  std::filesystem::path meta_path_;
  std::unique_ptr<io::OutputStream> out_stream_;
  uint32_t cache_cnt_ = 0;
};

}  // namespace psi::ecdh