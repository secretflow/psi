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
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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

  // Read at most `batch_size` items and return them. An empty returned vector
  // is treated as the end of stream.
  // Return extra count for each item in the batch.
  // key is the index of item in the batch, value is the count of extra items
  // for this item, if the value is x, the item will be (x + 1) times in the
  // origin file.
  virtual std::pair<std::vector<std::string>,
                    std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() {
    return std::make_pair(ReadNextBatch(),
                          std::unordered_map<uint32_t, uint32_t>());
  }
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
  struct ShuffledBatch {
    std::vector<std::string> batch_items;
    std::vector<size_t> batch_indices;
    std::vector<size_t> shuffled_indices;
    std::vector<uint32_t> dup_cnts;
  };

  explicit IShuffledBatchProvider() : IBatchProvider() {}

  virtual ~IShuffledBatchProvider() = default;

  // Read at most `batch_size` items and return data and shuffle index.
  // An empty returned vector is treated as the end of stream.
  virtual ShuffledBatch ReadNextShuffledBatch() = 0;
};

}  // namespace psi
