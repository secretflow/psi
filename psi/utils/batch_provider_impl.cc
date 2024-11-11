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

#include "psi/utils/batch_provider_impl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_map>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/batch_provider.h"
#include "psi/utils/key.h"

namespace psi {

namespace {
constexpr size_t kDefaultBatchSize = 1 << 20;
}

MemoryBatchProvider::MemoryBatchProvider(const std::vector<std::string>& items,
                                         size_t batch_size,
                                         const std::vector<std::string>& labels,
                                         bool enable_shuffle)
    : batch_size_(batch_size), items_(items), labels_(labels) {
  if (enable_shuffle) {
    buffer_shuffled_indices_.resize(items.size());
    std::iota(buffer_shuffled_indices_.begin(), buffer_shuffled_indices_.end(),
              0);
    std::mt19937 rng(yacl::crypto::SecureRandU64());
    std::shuffle(buffer_shuffled_indices_.begin(),
                 buffer_shuffled_indices_.end(), rng);
  }
}

std::vector<std::string> MemoryBatchProvider::ReadNextBatch() {
  std::vector<std::string> batch;
  YACL_ENFORCE(cursor_index_ <= items_.size());
  size_t n_items = std::min(batch_size_, items_.size() - cursor_index_);
  batch.insert(batch.end(), items_.begin() + cursor_index_,
               items_.begin() + cursor_index_ + n_items);
  cursor_index_ += n_items;
  return batch;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
MemoryBatchProvider::ReadNextLabeledBatch() {
  if (labels_.empty()) {
    YACL_THROW("unsupported.");
  }

  std::vector<std::string> batch_items;
  std::vector<std::string> batch_labels;

  YACL_ENFORCE(cursor_index_ <= items_.size());
  size_t n_items = std::min(batch_size_, items_.size() - cursor_index_);

  batch_items.insert(batch_items.end(), items_.begin() + cursor_index_,
                     items_.begin() + cursor_index_ + n_items);

  batch_labels.insert(batch_labels.end(), labels_.begin() + cursor_index_,
                      labels_.begin() + cursor_index_ + n_items);

  cursor_index_ += n_items;
  return std::make_pair(batch_items, batch_labels);
}

IShuffledBatchProvider::ShuffledBatch
MemoryBatchProvider::ReadNextShuffledBatch() {
  if (buffer_shuffled_indices_.empty()) {
    YACL_THROW("Empty data set.");
  }

  ShuffledBatch shuffled_batch;

  YACL_ENFORCE(cursor_index_ <= items_.size());
  size_t n_items = std::min(batch_size_, items_.size() - cursor_index_);
  for (size_t i = 0; i < n_items; ++i) {
    size_t shuffled_index = buffer_shuffled_indices_[cursor_index_ + i];
    shuffled_batch.batch_items.push_back(items_[shuffled_index]);
    shuffled_batch.batch_indices.push_back(cursor_index_ + i);
    shuffled_batch.shuffled_indices.push_back(shuffled_index);
  }

  cursor_index_ += n_items;

  return shuffled_batch;
}

const std::vector<std::string>& MemoryBatchProvider::items() const {
  return items_;
}

const std::vector<std::string>& MemoryBatchProvider::labels() const {
  if (labels_.empty()) {
    YACL_THROW("unsupported.");
  } else {
    return labels_;
  }
}

const std::vector<size_t>& MemoryBatchProvider::shuffled_indices() const {
  if (buffer_shuffled_indices_.empty()) {
    YACL_THROW("unsupported.");
  } else {
    return buffer_shuffled_indices_;
  }
}

CsvBatchProvider::CsvBatchProvider(const std::string& path,
                                   const std::vector<std::string>& item_fields,
                                   size_t batch_size,
                                   const std::vector<std::string>& label_fields)
    : batch_size_(batch_size), path_(path), item_analyzer_(path, item_fields) {
  in_ = io::BuildInputStream(io::FileIoOptions(path_));
  // skip header
  std::string line;
  in_->GetLine(&line);

  if (!label_fields.empty()) {
    label_analyzer_ = std::make_unique<CsvHeaderAnalyzer>(path, label_fields);
  }
}

std::vector<std::string> CsvBatchProvider::ReadNextBatch() {
  std::vector<std::string> ret;
  std::string line;
  while (in_->GetLine(&line)) {
    std::vector<absl::string_view> tokens = absl::StrSplit(line, ',');
    std::vector<absl::string_view> targets;
    for (size_t fidx : item_analyzer_.target_indices()) {
      YACL_ENFORCE(fidx < tokens.size(),
                   "Illegal line due to no field at index={}, line={}", fidx,
                   line);
      targets.push_back(absl::StripAsciiWhitespace(tokens[fidx]));
    }
    ret.push_back(KeysJoin(targets));
    if (ret.size() == batch_size_) {
      break;
    }
  }
  return ret;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
CsvBatchProvider::ReadNextLabeledBatch() {
  if (!label_analyzer_) {
    YACL_THROW("unsupported.");
  }

  std::pair<std::vector<std::string>, std::vector<std::string>> ret;
  std::string line;

  while (in_->GetLine(&line)) {
    std::vector<absl::string_view> tokens = absl::StrSplit(line, ',');
    std::vector<absl::string_view> items;
    std::vector<absl::string_view> labels;
    for (size_t fidx : item_analyzer_.target_indices()) {
      YACL_ENFORCE(fidx < tokens.size(),
                   "Illegal line due to no field at index={}, line={}", fidx,
                   line);
      items.push_back(absl::StripAsciiWhitespace(tokens[fidx]));
    }
    for (size_t fidx : label_analyzer_->target_indices()) {
      YACL_ENFORCE(fidx < tokens.size(),
                   "Illegal line due to no field at index={}, line={}", fidx,
                   line);
      labels.push_back(absl::StripAsciiWhitespace(tokens[fidx]));
    }

    ret.first.push_back(KeysJoin(items));
    ret.second.push_back(KeysJoin(labels));

    if (ret.first.size() == batch_size_) {
      break;
    }
  }

  return ret;
}

SimpleShuffledBatchProvider::SimpleShuffledBatchProvider(
    const std::string& path, const std::vector<std::string>& target_fields,
    size_t batch_size)
    : batch_size_(batch_size) {
  provider_ = std::make_shared<ArrowCsvBatchProvider>(
      path, target_fields, std::max(batch_size * 2, kDefaultBatchSize));
  Init();
}

SimpleShuffledBatchProvider::SimpleShuffledBatchProvider(
    const std::shared_ptr<IBasicBatchProvider>& provider, size_t batch_size)
    : batch_size_(batch_size), provider_(provider) {
  Init();
}

void SimpleShuffledBatchProvider::Init() {
  std::lock_guard lk(read_mutex_);
  buffer_ = ReadAndShuffle();
}

IShuffledBatchProvider::ShuffledBatch
SimpleShuffledBatchProvider::ReadNextShuffledBatch() {
  std::unique_lock lk(read_mutex_);

  ShuffledBatch shuffle_batch;

  size_t n_items = 0;
  if (read_end_) {
    return shuffle_batch;
  }

  while (n_items < batch_size_) {
    if (!buffer_.shuffled_indices.empty()) {
      size_t push_items =
          std::min(batch_size_ - n_items, buffer_.shuffled_indices.size());
      for (size_t i = 0; i < push_items; ++i) {
        shuffle_batch.dup_cnts.push_back(
            buffer_.dup_cnt[buffer_.shuffled_indices.front()]);
        shuffle_batch.batch_items.push_back(
            std::move(buffer_.items[buffer_.shuffled_indices.front()]));
        shuffle_batch.batch_indices.push_back(base_index_ + i);
        shuffle_batch.shuffled_indices.push_back(
            shuffle_base_index_ + buffer_.shuffled_indices.front());
        SPDLOG_DEBUG(
            "index {}, shuffle index: {}, read index: {}, shuffle base index: "
            "{} ",
            base_index_ + i,
            shuffle_base_index_ + buffer_.shuffled_indices.front(), base_index_,
            shuffle_base_index_);
        buffer_.shuffled_indices.pop_front();
      }
      n_items += push_items;
      base_index_ += push_items;
    } else {
      shuffle_base_index_ += buffer_.items.size();
      buffer_ = ReadAndShuffle();
      if (buffer_.items.empty()) {
        read_end_ = true;
        return shuffle_batch;
      }
    }
  }
  return shuffle_batch;
}

SimpleShuffledBatchProvider::RawBatch
SimpleShuffledBatchProvider::ReadAndShuffle() {
  auto read_proc = [&, this]() -> RawBatch {
    RawBatch ret;
    std::unordered_map<uint32_t, uint32_t> dup_cnt;
    std::tie(ret.items, dup_cnt) = provider_->ReadNextBatchWithDupCnt();
    ret.dup_cnt.resize(ret.items.size());
    for (auto& [k, v] : dup_cnt) {
      ret.dup_cnt[k] = v;
    }
    std::deque<size_t> shuffle_indices(ret.items.size());
    shuffle_indices.resize(ret.items.size());
    std::iota(shuffle_indices.begin(), shuffle_indices.end(), 0);
    std::mt19937 rng(yacl::crypto::SecureRandU64());
    std::shuffle(shuffle_indices.begin(), shuffle_indices.end(), rng);
    ret.shuffled_indices = std::move(shuffle_indices);

    return ret;
  };

  if (read_future_.valid()) {
    auto result = read_future_.get();
    if (!result.items.empty()) {
      read_future_ = std::async(std::launch::async, read_proc);
    }
    return result;
  } else {
    auto result = read_proc();
    read_future_ = std::async(std::launch::async, read_proc);
    return result;
  }
}

}  // namespace psi
