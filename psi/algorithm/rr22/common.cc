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

#include "psi/algorithm/rr22/common.h"

#include "omp.h"

#include "psi/algorithm/rr22/rr22_psi.h"
#include "psi/utils/bucket.h"

namespace psi::rr22 {

Rr22PsiOptions GenerateRr22PsiOptions(bool low_comm_mode) {
  Rr22PsiOptions options(kDefaultSSP, omp_get_num_procs(), kDefaultCompress);
  options.mode =
      low_comm_mode ? Rr22PsiMode::LowCommMode : Rr22PsiMode::FastMode;

  return options;
}

BucketDataStoreImpl::BucketDataStoreImpl(
    std::shared_ptr<yacl::link::Context> lctx,
    HashBucketCache* input_bucket_store,
    IndexWriter* intersection_indices_writer, RecoveryManager* recovery_manager)
    : IBucketDataStore(),
      input_bucket_store_(input_bucket_store),
      intersection_indices_writer_(intersection_indices_writer),
      recovery_manager_(recovery_manager),
      lctx_(std::move(lctx)) {
  self_sizes_ = std::vector<uint32_t>(input_bucket_store_->BucketNum());
  for (size_t i = 0; i < self_sizes_.size(); i++) {
    self_sizes_[i] = input_bucket_store_->GetBucketSize(i);
  }
  peer_sizes_ = std::vector<uint32_t>(input_bucket_store_->BucketNum());
  yacl::ByteContainerView buffer(self_sizes_.data(),
                                 self_sizes_.size() * sizeof(uint32_t));
  auto data = yacl::link::AllGather(lctx_, buffer, "exchange size");
  std::memcpy(peer_sizes_.data(), data[lctx_->NextRank()].data(),
              self_sizes_.size() * sizeof(uint32_t));
}

std::vector<HashBucketCache::BucketItem> BucketDataStoreImpl::GetBucketItems(
    size_t bucket_idx) {
  if (bucket_idx >= input_bucket_store_->BucketNum()) {
    return {};
  }
  return input_bucket_store_->LoadBucketItems(bucket_idx);
}

void BucketDataStoreImpl::WriteIntersetionItems(
    size_t bucket_idx, const std::vector<HashBucketCache::BucketItem>& items,
    const std::vector<uint32_t>& intersection_indices,
    const std::vector<uint32_t>& peer_dup_cnts) {
  for (size_t i = 0; i != intersection_indices.size(); ++i) {
    intersection_indices_writer_->WriteCache(
        items[intersection_indices[i]].index, peer_dup_cnts[i]);
  }
  intersection_indices_writer_->Commit();
  if (recovery_manager_ != nullptr) {
    recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
  }
}

std::pair<size_t, size_t> BucketDataStoreImpl::GetBucketDatasize(
    size_t bucket_idx) {
  return std::make_pair(self_sizes_[bucket_idx], peer_sizes_[bucket_idx]);
}

}  // namespace psi::rr22
