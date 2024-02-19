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

#include "psi/prelude.h"
#include "psi/utils/sync.h"

namespace psi {

std::optional<std::vector<HashBucketCache::BucketItem>> PrepareBucketData(
    v2::Protocol protocol, size_t bucket_idx,
    const std::shared_ptr<yacl::link::Context>& lctx,
    HashBucketCache* input_bucket_store) {
  std::vector<HashBucketCache::BucketItem> bucket_items_list;
  auto load_bucket_f = std::async([&] {
    bucket_items_list = input_bucket_store->LoadBucketItems(bucket_idx);
  });

  SyncWait(lctx, &load_bucket_f);

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
    BroadcastResult(lctx, &result_list);

    if (result_list.empty()) {
      return;
    }

    if (result_list.size() == bucket_items_list.size()) {
      for (const auto& item : bucket_items_list) {
        writer->WriteCache(item.index);
      }
    } else {
      std::sort(result_list.begin(), result_list.end());
      for (const auto& item : bucket_items_list) {
        if (std::binary_search(result_list.begin(), result_list.end(),
                               item.base64_data)) {
          writer->WriteCache(item.index);
        }
      }
    }

    writer->Commit();
  }
}

void HandleBucketResultByReceiver(
    bool broadcast_result, const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<HashBucketCache::BucketItem>& result_list,
    IndexWriter* writer) {
  if (broadcast_result) {
    std::vector<std::string> item_data_list;
    item_data_list.reserve(result_list.size());

    for (const auto& item : result_list) {
      item_data_list.emplace_back(item.base64_data);
    }

    BroadcastResult(lctx, &item_data_list);
  }

  for (const auto& item : result_list) {
    writer->WriteCache(item.index);
  }

  writer->Commit();
}

}  // namespace psi
