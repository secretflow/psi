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
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "psi/utils/batch_provider.h"
#include "psi/utils/csv_header_analyzer.h"
#include "psi/utils/io.h"

namespace psi {

class MemoryBatchProvider : public IBasicBatchProvider,
                            public ILabeledBatchProvider,
                            public IShuffledBatchProvider {
 public:
  MemoryBatchProvider(const std::vector<std::string>& items, size_t batch_size,
                      const std::vector<std::string>& labels = {},
                      bool enable_shuffle = false);

  std::vector<std::string> ReadNextBatch() override;

  std::pair<std::vector<std::string>, std::vector<std::string>>
  ReadNextLabeledBatch() override;

  ShuffledBatch ReadNextShuffledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

  [[nodiscard]] const std::vector<std::string>& items() const;

  [[nodiscard]] const std::vector<std::string>& labels() const;

  [[nodiscard]] const std::vector<size_t>& shuffled_indices() const;

 private:
  const size_t batch_size_;
  const std::vector<std::string>& items_;
  const std::vector<std::string>& labels_;
  std::vector<size_t> buffer_shuffled_indices_;
  size_t cursor_index_ = 0;
};

class CsvBatchProvider : public IBasicBatchProvider,
                         public ILabeledBatchProvider {
 public:
  CsvBatchProvider(const std::string& path,
                   const std::vector<std::string>& item_fields,
                   size_t batch_size,
                   const std::vector<std::string>& label_fields = {});

  std::vector<std::string> ReadNextBatch() override;

  std::pair<std::vector<std::string>, std::vector<std::string>>
  ReadNextLabeledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  const size_t batch_size_;
  const std::string path_;
  std::unique_ptr<io::InputStream> in_;
  CsvHeaderAnalyzer item_analyzer_;
  std::unique_ptr<CsvHeaderAnalyzer> label_analyzer_;
};

// NOTE(junfeng):
// SimpleShuffledBatchProvider consists a IBasicBatchProvider to provide data
// and two buffers to speed-up reading.
// When SimpleShuffledBatchProvider reads one buffer, IBasicBatchProvider
// loads a new batch to the other buffer at the same time.
// 1. batch_size indicates the size of returns of ReadNextShuffledBatch.
// 2. provider_batch_size indicates the batch size of IBasicBatchProvider, or
// the size of buffers. provider_batch_size should be greater than batch_size.
// 3. If a IBasicBatchProvider is not provided, a default CsvBatchProvider will
// be constructed.
class SimpleShuffledBatchProvider : public IShuffledBatchProvider {
 public:
  SimpleShuffledBatchProvider(const std::string& path,
                              const std::vector<std::string>& target_fields,
                              size_t batch_size);

  explicit SimpleShuffledBatchProvider(
      const std::shared_ptr<IBasicBatchProvider>& provider, size_t batch_size);

  ShuffledBatch ReadNextShuffledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  void Init();

  struct RawBatch {
    std::vector<std::string> items;
    std::deque<size_t> shuffled_indices;
    std::vector<uint32_t> dup_cnt;
  };
  RawBatch ReadAndShuffle();

 private:
  std::mutex read_mutex_;
  size_t batch_size_ = 0;
  std::shared_ptr<IBasicBatchProvider> provider_;

  std::future<RawBatch> read_future_;
  size_t shuffle_base_index_ = 0;
  size_t base_index_ = 0;
  RawBatch buffer_;

  bool read_end_ = false;
};

}  // namespace psi
