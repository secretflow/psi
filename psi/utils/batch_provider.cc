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

#include "psi/utils/batch_provider.h"

#include <algorithm>
#include <future>
#include <memory>
#include <random>

#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/utils/key.h"

namespace psi {

MemoryBatchProvider::MemoryBatchProvider(const std::vector<std::string>& items,
                                         size_t batch_size,
                                         const std::vector<std::string>& labels,
                                         bool enable_shuffle)
    : batch_size_(batch_size), items_(items), labels_(labels) {
  if (enable_shuffle) {
    shuffled_indices_.resize(items.size());
    std::iota(shuffled_indices_.begin(), shuffled_indices_.end(), 0);
    std::mt19937 rng(yacl::crypto::SecureRandU64());
    std::shuffle(shuffled_indices_.begin(), shuffled_indices_.end(), rng);
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

std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>>
MemoryBatchProvider::ReadNextShuffledBatch() {
  if (shuffled_indices_.empty()) {
    YACL_THROW("unsupported.");
  }

  std::vector<std::string> batch_data;
  std::vector<size_t> batch_indices;
  std::vector<size_t> shuffle_indices;

  YACL_ENFORCE(cursor_index_ <= items_.size());
  size_t n_items = std::min(batch_size_, items_.size() - cursor_index_);
  for (size_t i = 0; i < n_items; ++i) {
    size_t shuffled_index = shuffled_indices_[cursor_index_ + i];
    batch_data.push_back(items_[shuffled_index]);
    batch_indices.push_back(cursor_index_ + i);
    shuffle_indices.push_back(shuffled_index);
  }

  cursor_index_ += n_items;

  return std::make_tuple(batch_data, batch_indices, shuffle_indices);
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
  if (shuffled_indices_.empty()) {
    YACL_THROW("unsupported.");
  } else {
    return shuffled_indices_;
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
    size_t batch_size, size_t provider_batch_size, bool shuffle)
    : batch_size_(batch_size),
      provider_batch_size_(provider_batch_size),
      shuffle_(shuffle) {
  provider_ = std::make_shared<CsvBatchProvider>(path, target_fields,
                                                 provider_batch_size);
  Init();
}

SimpleShuffledBatchProvider::SimpleShuffledBatchProvider(
    const std::shared_ptr<IBasicBatchProvider>& provider, size_t batch_size,
    bool shuffle)
    : batch_size_(batch_size),
      provider_(provider),
      provider_batch_size_(provider->batch_size()),
      shuffle_(shuffle) {
  Init();
}

void SimpleShuffledBatchProvider::Init() {
  YACL_ENFORCE_GE(provider_batch_size_, 2 * batch_size_);
  ReadAndShuffle(0, true);
}

std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>>
SimpleShuffledBatchProvider::ReadNextShuffledBatch() {
  std::unique_lock lk(read_mutex_);

  std::vector<std::string> batch_data;
  std::vector<size_t> batch_indices;
  std::vector<size_t> shuffle_indices;
  size_t n_items = 0;
  size_t next_index = 1 - bucket_index_;

  {
    std::unique_lock lk(bucket_mutex_[bucket_index_]);

    if (cursor_index_ == 0) {
      if (!file_end_flag_) {
        ReadAndShuffle(next_index, false);
      }
    }

    if (file_end_flag_ && bucket_items_[bucket_index_].empty()) {
      SPDLOG_INFO("cursor_index_:{}, bucket_index_:{}, {}-{}", cursor_index_,
                  bucket_index_, bucket_items_[0].size(),
                  bucket_items_[1].size());
      return std::make_tuple(batch_data, batch_indices, shuffle_indices);
    }
    YACL_ENFORCE_LE(cursor_index_, bucket_items_[bucket_index_].size());

    size_t n_items = std::min(
        batch_size_, bucket_items_[bucket_index_].size() - cursor_index_);

    for (size_t i = 0; i < n_items; ++i) {
      size_t shuffled_index =
          shuffled_indices_[bucket_index_][cursor_index_ + i];
      batch_data.push_back(bucket_items_[bucket_index_][shuffled_index]);
      batch_indices.push_back(bucket_count_ * provider_batch_size_ +
                              cursor_index_ + i);
      shuffle_indices.push_back(bucket_count_ * provider_batch_size_ +
                                shuffled_index);
    }

    cursor_index_ += n_items;

    bool finished = false;

    if (cursor_index_ == bucket_items_[bucket_index_].size()) {
      bucket_items_[bucket_index_].resize(0);
      cursor_index_ = 0;
      finished = true;
    }

    if (n_items == batch_size_) {
      if (finished) {
        bucket_index_ = next_index;
      }

      SPDLOG_INFO("cursor_index_:{}, bucket_index_:{}, {}-{}", cursor_index_,
                  bucket_index_, bucket_items_[0].size(),
                  bucket_items_[1].size());
      return std::make_tuple(batch_data, batch_indices, shuffle_indices);
    }
  }

  if (!file_end_flag_) {
    ReadAndShuffle(bucket_index_, false);
  }

  {
    std::unique_lock lk(bucket_mutex_[next_index]);

    if (!bucket_items_[next_index].empty()) {
      size_t left_size = batch_size_ - n_items;

      cursor_index_ = 0;
      bucket_count_++;
      size_t m_items = std::min(left_size, bucket_items_[next_index].size());

      for (size_t i = 0; i < m_items; ++i) {
        size_t shuffled_index =
            shuffled_indices_[next_index][cursor_index_ + i];
        batch_data.push_back(bucket_items_[next_index][shuffled_index]);
        batch_indices.push_back(bucket_count_ * provider_batch_size_ +
                                cursor_index_ + i);
        shuffle_indices.push_back(bucket_count_ * provider_batch_size_ +
                                  shuffled_index);
      }

      if (m_items == bucket_items_[next_index].size()) {
        cursor_index_ = 0;
        bucket_items_[next_index].resize(0);
      } else {
        cursor_index_ += m_items;
      }
      n_items += m_items;

      bucket_index_ = next_index;
    }
  }

  SPDLOG_INFO("cursor_index_:{}, bucket_index_:{}, {}-{}", cursor_index_,
              bucket_index_, bucket_items_[0].size(), bucket_items_[1].size());
  return std::make_tuple(batch_data, batch_indices, shuffle_indices);
}

void SimpleShuffledBatchProvider::ReadAndShuffle(size_t read_index,
                                                 bool blocked) {
  auto read_proc = [&](int idx) -> void {
    std::unique_lock<std::mutex> lk(bucket_mutex_[idx]);

    SPDLOG_INFO("ReadAndShuffle start, idx:{}, provider_batch_size:{}", idx,
                provider_batch_size_);

    {
      std::unique_lock<std::mutex> file_lk(file_mutex_);
      bucket_items_[idx] = provider_->ReadNextBatch();
      if (bucket_items_[idx].empty() ||
          (bucket_items_[idx].size() < provider_batch_size_)) {
        file_end_flag_ = true;
      }

      shuffled_indices_[idx].resize(bucket_items_[idx].size());
      std::iota(shuffled_indices_[idx].begin(), shuffled_indices_[idx].end(),
                0);
    }

    if (shuffle_ && !bucket_items_[idx].empty()) {
      std::mt19937 rng(yacl::crypto::SecureRandU64());
      std::shuffle(shuffled_indices_[idx].begin(), shuffled_indices_[idx].end(),
                   rng);
    }

    SPDLOG_INFO("ReadAndShuffle end, idx:{} , size:{}", idx,
                bucket_items_[idx].size());
  };

  f_read_[read_index] = std::async(std::launch::async, read_proc, read_index);
  if (blocked) {
    f_read_[read_index].get();
  }
}

}  // namespace psi
