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

#include "psi/utils/bucket.h"

#include <cstdint>
#include <unordered_map>

#include "yacl/crypto/hash/hash_utils.h"

#include "psi/prelude.h"
#include "psi/utils/sync.h"

namespace psi {

void CalcBucketItemSecHash(std::vector<HashBucketCache::BucketItem>& items) {
  yacl::parallel_for(0, items.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      items[i].sec_hash = yacl::crypto::Blake3_128(items[i].base64_data);
    }
  });
}

std::optional<std::vector<HashBucketCache::BucketItem>> PrepareBucketData(
    v2::Protocol protocol, size_t bucket_idx,
    const std::shared_ptr<yacl::link::Context>& lctx,
    HashBucketCache* input_bucket_store) {
  std::vector<HashBucketCache::BucketItem> bucket_items_list;

  SyncWait(lctx, [&] {
    bucket_items_list = input_bucket_store->LoadBucketItems(bucket_idx);
  });

  size_t min_inputs_size = bucket_items_list.size();
  std::vector<size_t> inputs_size_list =
      AllGatherItemsSize(lctx, bucket_items_list.size());
  for (size_t idx = 0; idx < inputs_size_list.size(); idx++) {
    SPDLOG_INFO("psi protocol={}, rank={}, inputs_size={}", protocol, idx,
                inputs_size_list[idx]);
    min_inputs_size = std::min(min_inputs_size, inputs_size_list[idx]);
  }

  if (min_inputs_size == 0) {
    SPDLOG_INFO(
        "psi protocol={}, min_inputs_size=0, "
        "no need do intersection",
        protocol);
    return {};
  }

  SPDLOG_INFO("run psi bucket_idx={}, bucket_item_size={} ", bucket_idx,
              bucket_items_list.size());

  return bucket_items_list;
}

void HandleBucketResultBySender(
    bool broadcast_result, const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<HashBucketCache::BucketItem>& bucket_items_list,
    IndexWriter* writer) {
  if (broadcast_result) {
    std::vector<std::string> result_list;
    std::unordered_map<uint64_t, uint32_t> duplicate_item_cnt;

    BroadcastResult(lctx, &result_list, &duplicate_item_cnt);

    if (result_list.empty()) {
      return;
    }
    std::unordered_map<std::string, uint32_t> peer_result;
    for (size_t i = 0; i != result_list.size(); ++i) {
      peer_result[result_list[i]] = duplicate_item_cnt[i];
    }

    if (result_list.size() == bucket_items_list.size()) {
      for (const auto& item : bucket_items_list) {
        writer->WriteCache(item.index, peer_result[item.base64_data]);
      }
    } else {
      std::sort(result_list.begin(), result_list.end());
      for (const auto& item : bucket_items_list) {
        auto iter = peer_result.find(item.base64_data);
        if (iter != peer_result.end()) {
          writer->WriteCache(item.index, iter->second);
        }
      }
    }

    writer->Commit();
  }
}

void HandleBucketResultByReceiver(
    bool broadcast_result, const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<HashBucketCache::BucketItem>& result_list,
    const std::vector<uint32_t>& peer_extra_dup_cnt, IndexWriter* writer) {
  if (broadcast_result) {
    std::vector<std::string> item_data_list;
    item_data_list.reserve(result_list.size());
    std::unordered_map<uint64_t, uint32_t> duplicate_item_cnt;
    for (size_t i = 0; i != result_list.size(); ++i) {
      item_data_list.emplace_back(result_list[i].base64_data);
      if (result_list[i].extra_dup_cnt > 0) {
        duplicate_item_cnt[i] = result_list[i].extra_dup_cnt;
      }
    }

    BroadcastResult(lctx, &item_data_list, &duplicate_item_cnt);
  }

  for (size_t i = 0; i < result_list.size(); ++i) {
    writer->WriteCache(result_list[i].index, peer_extra_dup_cnt[i]);
  }

  writer->Commit();
}

bool HashListEqualTest(const std::vector<yacl::Buffer>& hash_list) {
  YACL_ENFORCE(!hash_list.empty(), "unsupported hash_list size={}",
               hash_list.size());
  for (size_t idx = 1; idx < hash_list.size(); idx++) {
    if (hash_list[idx] == hash_list[0]) {
      continue;
    }
    return false;
  }
  return true;
}

size_t NegotiateBucketNum(const std::shared_ptr<yacl::link::Context>& lctx,
                          size_t self_items_count, size_t self_bucket_size,
                          int psi_type) {
  std::vector<size_t> items_size_list =
      AllGatherItemsSize(lctx, self_items_count);

  std::vector<size_t> bucket_count_list(items_size_list.size());
  size_t max_bucket_count = 0;
  size_t min_item_size = self_items_count;

  for (size_t idx = 0; idx < items_size_list.size(); idx++) {
    bucket_count_list[idx] =
        (items_size_list[idx] + self_bucket_size - 1) / self_bucket_size;
    max_bucket_count = std::max(max_bucket_count, bucket_count_list[idx]);
    min_item_size = std::min(min_item_size, items_size_list[idx]);

    SPDLOG_INFO("psi protocol={}, rank={} item_size={}", psi_type, idx,
                items_size_list[idx]);
  }

  // one party item_size is 0, no need to do intersection
  if (min_item_size == 0) {
    SPDLOG_INFO("psi protocol={}, min_item_size=0", psi_type);
    return 0;
  }

  return max_bucket_count;
}

}  // namespace psi
