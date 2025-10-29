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

#include <optional>

#include "yacl/link/link.h"

#include "psi/checkpoint/recovery.h"
#include "psi/utils/hash_bucket_cache.h"
#include "psi/utils/index_store.h"

namespace psi {

// Default bucket size when not provided.
constexpr uint64_t kDefaultBucketSize = 1 << 20;

void CalcBucketItemSecHash(std::vector<HashBucketCache::BucketItem>& items);

std::optional<std::vector<HashBucketCache::BucketItem>> PrepareBucketData(
    v2::Protocol protocol, size_t bucket_idx,
    const std::shared_ptr<yacl::link::Context>& lctx,
    HashBucketCache* input_bucket_store);

void HandleBucketResultBySender(
    bool broadcast_result, const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<HashBucketCache::BucketItem>& bucket_items_list,
    IndexWriter* writer);

void HandleBucketResultByReceiver(
    bool broadcast_result, const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<HashBucketCache::BucketItem>& result_list,
    const std::vector<uint32_t>& peer_extra_dup_cnt, IndexWriter* writer);

bool HashListEqualTest(const std::vector<yacl::Buffer>& hash_list);

size_t NegotiateBucketNum(const std::shared_ptr<yacl::link::Context>& lctx,
                          size_t self_items_count, size_t self_bucket_size,
                          int psi_type);
}  // namespace psi
