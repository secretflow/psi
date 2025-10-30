// Copyright 2025 Ant Group Co., Ltd.
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

#include "psi/algorithm/rr22/rr22_operator.h"

#include <memory>

namespace psi::rr22 {

Rr22Operator::Rr22Operator(Options opts,
                           std::shared_ptr<IDataStore> input_store,
                           std::shared_ptr<IResultStore> output_store)
    : PsiOperator(opts.lctx, std::move(input_store), std::move(output_store),
                  opts.recovery_manager, opts.broadcast_result),
      opts_(std::move(opts)) {}

bool Rr22Operator::ReceiveResult() {
  return opts_.receiver_rank == opts_.lctx->Rank() || opts_.broadcast_result;
}

void Rr22Operator::OnInit() {}

void Rr22Operator::OnRun() {
  PreProcessFunc pre_process_func =
      [this](size_t bucket_idx) -> std::vector<HashBucketCache::BucketItem> {
    std::vector<HashBucketCache::BucketItem> bucket_items;
    auto provider = input_store_->Load(bucket_idx);
    auto item_datas = provider->ReadAll();
    bucket_items.reserve(item_datas.size());
    for (size_t i = 0; i < item_datas.size(); ++i) {
      bucket_items.emplace_back(HashBucketCache::BucketItem{
          .index = i,
          .extra_dup_cnt = item_datas[i].cnt - 1,
          .base64_data = std::move(item_datas[i].buf)});
    }
    return bucket_items;
  };
  PostProcessFunc post_process_func =
      [this](size_t bucket_idx,
             const std::vector<HashBucketCache::BucketItem>& bucket_items,
             const std::vector<uint32_t>& intersection_indices,
             const std::vector<uint32_t>& peer_dup_cnts) {
        std::vector<PsiResultIndex> indices;
        indices.reserve(intersection_indices.size());

        for (size_t i = 0; i < intersection_indices.size(); ++i) {
          indices.emplace_back(PsiResultIndex{
              .data = bucket_items[intersection_indices[i]].index,
              .peer_item_cnt = peer_dup_cnts[i] + 1});
        }
        auto recevier = output_store_->GetReceiver(bucket_idx);
        recevier->Add(std::move(indices));
      };

  size_t bucket_idx =
      recovery_manager_
          ? std::min(recovery_manager_->parsed_bucket_count_from_peer(),
                     recovery_manager_->checkpoint().parsed_bucket_count())
          : 0;

  Rr22Runner runner(link_ctx_, opts_.rr22_opts, input_store_->GetBucketNum(),
                    opts_.broadcast_result, pre_process_func,
                    post_process_func);

  if (opts_.pipeline_mode) {
    runner.AsyncRun(bucket_idx, opts_.lctx->Rank() != opts_.receiver_rank);
  } else {
    runner.ParallelRun(bucket_idx, opts_.lctx->Rank() != opts_.receiver_rank,
                       opts_.parallel_level);
  }
}

void Rr22Operator::OnEnd() {}

}  // namespace psi::rr22
