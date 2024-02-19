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

#include "psi/utils/ec_point_store.h"

#include <omp.h>

#include <algorithm>
#include <future>
#include <optional>
#include <unordered_set>
#include <utility>

#include "absl/strings/escaping.h"
#include "spdlog/spdlog.h"

#include "psi/utils/batch_provider.h"

namespace psi {

void MemoryEcPointStore::Save(std::string ciphertext) {
  store_.push_back(std::move(ciphertext));
  item_cnt_++;
}

HashBucketEcPointStore::HashBucketEcPointStore(const std::string& cache_dir,
                                               size_t num_bins,
                                               bool use_scoped_tmp_dir)
    : num_bins_(num_bins) {
  cache_ = std::make_unique<HashBucketCache>(cache_dir, num_bins,
                                             use_scoped_tmp_dir);
}

void HashBucketEcPointStore::Save(std::string ciphertext) {
  cache_->WriteItem(ciphertext);
}

uint64_t HashBucketEcPointStore::ItemCount() { return cache_->ItemCount(); }

HashBucketEcPointStore::~HashBucketEcPointStore() { Flush(); }

std::vector<uint64_t> FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer) {
  YACL_ENFORCE_EQ(self->num_bins(), peer->num_bins());
  self->Flush();
  peer->Flush();

  // Compute indices
  std::vector<uint64_t> indices;
  for (size_t bin_idx = 0; bin_idx < self->num_bins(); ++bin_idx) {
    std::vector<HashBucketCache::BucketItem> self_results =
        self->LoadBucketItems(bin_idx);
    std::vector<HashBucketCache::BucketItem> peer_results =
        peer->LoadBucketItems(bin_idx);
    std::unordered_set<std::string> peer_set;
    peer_set.reserve(peer_results.size());
    std::for_each(peer_results.begin(), peer_results.end(),
                  [&](HashBucketCache::BucketItem& item) {
                    peer_set.insert(std::move(item.base64_data));
                  });
    for (const auto& item : self_results) {
      if (peer_set.find(item.base64_data) != peer_set.end()) {
        indices.push_back(item.index);
      }
    }
  }
  // Sort to make `FilterFileByIndices` happy.
  std::sort(indices.begin(), indices.end());
  return indices;
}

void FinalizeAndComputeIndices(
    const std::shared_ptr<HashBucketEcPointStore>& self,
    const std::shared_ptr<HashBucketEcPointStore>& peer,
    IndexWriter* index_writer) {
  YACL_ENFORCE_EQ(self->num_bins(), peer->num_bins());
  self->Flush();
  peer->Flush();

  // Compute indices
  for (size_t bin_idx = 0; bin_idx < self->num_bins(); ++bin_idx) {
    std::vector<HashBucketCache::BucketItem> self_results =
        self->LoadBucketItems(bin_idx);
    std::vector<HashBucketCache::BucketItem> peer_results =
        peer->LoadBucketItems(bin_idx);
    std::unordered_set<std::string> peer_set;
    peer_set.reserve(peer_results.size());
    std::for_each(peer_results.begin(), peer_results.end(),
                  [&](HashBucketCache::BucketItem& item) {
                    peer_set.insert(std::move(item.base64_data));
                  });
    for (const auto& item : self_results) {
      if (peer_set.find(item.base64_data) != peer_set.end()) {
        index_writer->WriteCache(item.index);
      }
    }

    index_writer->Commit();
  }
}

const std::string CachedCsvEcPointStore::cipher_id = "id";

CachedCsvEcPointStore::CachedCsvEcPointStore(const std::string& path,
                                             bool enable_cache,
                                             const std::string& party,
                                             bool read_only)
    : path_(path),
      enable_cache_(enable_cache),
      party_(party),
      read_only_(read_only) {
  if (!read_only) {
    output_stream_ = io::BuildOutputStream(io::FileIoOptions(path));
    output_stream_->Write(fmt::format("{}\n", cipher_id));
  }
}

CachedCsvEcPointStore::~CachedCsvEcPointStore() {
  if (!read_only_) {
    output_stream_->Close();
  }
}

void CachedCsvEcPointStore::Save(std::string ciphertext) {
  output_stream_->Write(fmt::format("{}\n", ciphertext));
  if (enable_cache_) {
    cache_.insert({ciphertext, item_cnt_});
  }

  item_cnt_++;
  if (item_cnt_ % 10000000 == 0) {
    SPDLOG_INFO("{} item_cnt_={}", party_, item_cnt_);
  }
}

void CachedCsvEcPointStore::Save(const std::vector<std::string>& ciphertext) {
  for (const auto& text : ciphertext) {
    output_stream_->Write(fmt::format("{}\n", text));
    if (enable_cache_) {
      cache_.insert({text, item_cnt_});
    }

    item_cnt_++;
    if (item_cnt_ % 10000000 == 0) {
      SPDLOG_INFO("{} item_cnt_={}", party_, item_cnt_);
    }
  }
}

std::pair<std::vector<uint64_t>, std::vector<std::string>>
FinalizeAndComputeIndices(const std::shared_ptr<CachedCsvEcPointStore>& self,
                          const std::shared_ptr<CachedCsvEcPointStore>& peer,
                          size_t batch_size) {
  self->Flush();
  peer->Flush();

  SPDLOG_INFO("Begin FinalizeAndComputeIndices");

  std::vector<uint64_t> indices;
  std::vector<std::string> masked_items;

  std::vector<std::string> ids = {CachedCsvEcPointStore::cipher_id};
  CsvBatchProvider peer_provider(peer->Path(), ids, batch_size);
  size_t batch_count = 0;
  size_t compare_thread_num = omp_get_num_procs();

  while (true) {
    SPDLOG_INFO("begin read compare batch {}", batch_count);
    std::vector<std::string> batch_peer_data = peer_provider.ReadNextBatch();
    SPDLOG_INFO("end read compare batch {}", batch_count);

    if (batch_peer_data.empty()) {
      break;
    }

    size_t compare_size =
        (batch_peer_data.size() + compare_thread_num - 1) / compare_thread_num;

    std::vector<std::vector<uint64_t>> batch_indices(compare_thread_num);
    std::vector<std::vector<std::string>> batch_masked_items(
        compare_thread_num);

    auto compare_proc = [&](int idx) -> void {
      uint64_t begin = idx * compare_size;
      uint64_t end =
          std::min<uint64_t>(batch_peer_data.size(), begin + compare_size);

      for (size_t i = begin; i < end; i++) {
        auto search_ret = self->SearchIndex(batch_peer_data[i]);
        if (search_ret.has_value()) {
          batch_indices[idx].push_back(search_ret.value());
          batch_masked_items[idx].push_back(batch_peer_data[i]);
        }
      }
    };

    std::vector<std::future<void>> f_compare(compare_thread_num);
    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i] = std::async(compare_proc, i);
    }

    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i].get();
    }

    batch_count++;

    for (const auto& r : batch_indices) {
      indices.insert(indices.end(), r.begin(), r.end());
    }
    for (const auto& r : batch_masked_items) {
      masked_items.insert(masked_items.end(), r.begin(), r.end());
    }
    SPDLOG_INFO("FinalizeAndComputeIndices, batch_count:{}", batch_count);
  }

  SPDLOG_INFO("End FinalizeAndComputeIndices, batch_count:{}", batch_count);
  return std::make_pair(indices, masked_items);
}

std::vector<uint64_t> GetIndicesByItems(
    const std::string& input_path,
    const std::vector<std::string>& selected_fields,
    const std::vector<std::string>& items, size_t batch_size) {
  std::vector<uint64_t> indices;

  std::unordered_set<std::string> items_set;

  items_set.insert(items.begin(), items.end());

  auto batch_provider = std::make_shared<CsvBatchProvider>(
      input_path, selected_fields, batch_size);

  size_t compare_thread_num = omp_get_num_procs();

  size_t item_index = 0;
  size_t batch_count = 0;
  while (true) {
    auto batch_items = batch_provider->ReadNextBatch();
    if (batch_items.empty()) {
      break;
    }

    size_t compare_size =
        (batch_items.size() + compare_thread_num - 1) / compare_thread_num;

    std::vector<std::vector<uint64_t>> result(compare_thread_num);

    auto compare_proc = [&](int idx) -> void {
      uint64_t begin = idx * compare_size;
      uint64_t end = std::min<size_t>(batch_items.size(), begin + compare_size);

      for (uint64_t i = begin; i < end; i++) {
        auto search_ret = items_set.find(batch_items[i]);
        if (search_ret != items_set.end()) {
          result[idx].push_back(item_index + i);
        }
      }
    };

    std::vector<std::future<void>> f_compare(compare_thread_num);
    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i] = std::async(compare_proc, i);
    }

    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i].get();
    }

    for (const auto& r : result) {
      indices.insert(indices.end(), r.begin(), r.end());
    }

    batch_count++;

    item_index += batch_items.size();
    SPDLOG_INFO("GetIndices batch count:{}, item_index:{}", batch_count,
                item_index);
  }
  SPDLOG_INFO("Finish GetIndices, indices size:{}", indices.size());

  return indices;
}

}  // namespace psi
