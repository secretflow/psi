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
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "psi/utils/csv_header_analyzer.h"
#include "psi/utils/io.h"

namespace psi {

/// Interface which produce batch of strings.
class IBatchProvider {
 public:
  virtual ~IBatchProvider() = default;

  [[nodiscard]] virtual size_t batch_size() const = 0;
};

class IBasicBatchProvider : virtual public IBatchProvider {
 public:
  explicit IBasicBatchProvider() : IBatchProvider() {}

  virtual ~IBasicBatchProvider() = default;

  // Read at most `batch_size` items and return them. An empty returned vector
  // is treated as the end of stream.
  virtual std::vector<std::string> ReadNextBatch() = 0;
};

class ILabeledBatchProvider : virtual public IBatchProvider {
 public:
  explicit ILabeledBatchProvider() : IBatchProvider() {}

  virtual ~ILabeledBatchProvider() = default;

  // Read at most `batch_size` items&labels and return them. An empty returned
  // vector is treated as the end of stream.
  virtual std::pair<std::vector<std::string>, std::vector<std::string>>
  ReadNextLabeledBatch() = 0;
};

class IShuffledBatchProvider : virtual public IBatchProvider {
 public:
  explicit IShuffledBatchProvider() : IBatchProvider() {}

  virtual ~IShuffledBatchProvider() = default;

  // Read at most `batch_size` items and return data and shuffle index.
  // An empty returned vector is treated as the end of stream.
  virtual std::tuple<std::vector<std::string>, std::vector<size_t>,
                     std::vector<size_t>>
  ReadNextShuffledBatch() = 0;
};

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

  std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>>
  ReadNextShuffledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

  [[nodiscard]] const std::vector<std::string>& items() const;

  [[nodiscard]] const std::vector<std::string>& labels() const;

  [[nodiscard]] const std::vector<size_t>& shuffled_indices() const;

 private:
  const size_t batch_size_;
  const std::vector<std::string>& items_;
  const std::vector<std::string>& labels_;
  std::vector<size_t> shuffled_indices_;
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
                              size_t batch_size,
                              size_t provider_batch_size = 100000000,
                              bool shuffle = false);

  explicit SimpleShuffledBatchProvider(
      const std::shared_ptr<IBasicBatchProvider>& provider, size_t batch_size,
      bool shuffle = false);

  std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>>
  ReadNextShuffledBatch() override;

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  void Init();

  void ReadAndShuffle(size_t read_index, bool blocked);

  const size_t batch_size_;
  std::shared_ptr<IBasicBatchProvider> provider_;
  size_t provider_batch_size_;
  bool shuffle_;

  std::array<std::vector<std::string>, 2> bucket_items_;
  std::array<std::vector<size_t>, 2> shuffled_indices_;
  size_t cursor_index_ = 0;
  size_t bucket_index_ = 0;
  size_t bucket_count_ = 0;

  std::array<std::future<void>, 2> f_read_;

  std::array<std::mutex, 2> bucket_mutex_;
  std::mutex read_mutex_;
  std::mutex file_mutex_;
  bool file_end_flag_ = false;
};

}  // namespace psi
