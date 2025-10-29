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

#include "psi/algorithm/psi_operator.h"

#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi {

namespace {

void BroadcastIntersection(const std::shared_ptr<yacl::link::Context>& link_ctx,
                           const std::vector<PsiItemData>& inputs,
                           std::vector<PsiResultIndex>& intersection) {
  std::vector<std::string> item_data_list;
  std::unordered_map<ItemIndexType, ItemCntType> dup_item_cnt;
  if (intersection.size() > 0) {
    item_data_list.reserve(intersection.size());

    for (size_t i = 0; i != intersection.size(); ++i) {
      auto index = intersection[i].data;
      item_data_list.emplace_back(inputs[index].buf);
      if (inputs[index].cnt > 1) {
        dup_item_cnt[i] = inputs[index].cnt;
      }
    }
    BroadcastResult(link_ctx, &item_data_list, &dup_item_cnt);
  } else {
    BroadcastResult(link_ctx, &item_data_list, &dup_item_cnt);

    if (item_data_list.size() > 0) {
      intersection.reserve(item_data_list.size());

      std::unordered_map<std::string, size_t> data_index_map;
      for (size_t i = 0; i != item_data_list.size(); ++i) {
        data_index_map[item_data_list[i]] = i;
      }
      for (size_t i = 0; i != inputs.size(); ++i) {
        auto iter = data_index_map.find(inputs[i].buf);
        if (iter != data_index_map.end()) {
          PsiResultIndex res_index{.data = i};
          // deal dup_item_cnt
          auto cnt_iter = dup_item_cnt.find(iter->second);
          if (cnt_iter != dup_item_cnt.end()) {
            res_index.peer_item_cnt = cnt_iter->second;
          }
          intersection.emplace_back(std::move(res_index));
        }
      }
    }
  }
}

}  // namespace

PsiOperator::PsiOperator(std::shared_ptr<yacl::link::Context> link_ctx,
                         std::shared_ptr<IDataStore> input_store,
                         std::shared_ptr<IResultStore> output_store,
                         std::shared_ptr<RecoveryManager> recovery_manager,
                         bool broadcast_result)
    : link_ctx_(std::move(link_ctx)),
      input_store_(std::move(input_store)),
      output_store_(std::move(output_store)),
      recovery_manager_(std::move(recovery_manager)),
      broadcast_result_(broadcast_result) {
  YACL_ENFORCE(link_ctx_);
  YACL_ENFORCE(input_store_);
  YACL_ENFORCE(output_store_);

  YACL_ENFORCE_EQ(link_ctx_->WorldSize(), 2U);
}

void PsiOperator::Init() {
  SyncWait(link_ctx_, [this] { OnInit(); });
}

void PsiOperator::Run() {
  if (SupportStreaming()) {
    YACL_ENFORCE_EQ(input_store_->GetBucketNum(), 1U);
    SyncWait(link_ctx_, [this] { OnRun(); });
  } else if (SupportBucketParallel()) {
    SyncWait(link_ctx_, [this] { OnRun(); });
  } else {
    OnSequenceRun();
  }
}

void PsiOperator::OnSequenceRun() {
  auto bucket_num = input_store_->GetBucketNum();

  size_t bucket_idx =
      recovery_manager_
          ? std::min(recovery_manager_->parsed_bucket_count_from_peer(),
                     recovery_manager_->checkpoint().parsed_bucket_count())
          : 0;

  for (; bucket_idx != bucket_num; ++bucket_idx) {
    auto provider = input_store_->Load(bucket_idx);
    auto receiver = output_store_->GetReceiver(bucket_idx);

    // allgather dup item count
    auto item_datas = provider->ReadAll();
    // run psi algo core
    auto intersection = SyncWait(link_ctx_, [&] { return OnRun(item_datas); });

    // broadcast result
    if (broadcast_result_) {
      BroadcastIntersection(link_ctx_, item_datas, intersection);
    }

    // write result
    SyncWait(link_ctx_, [&] {
      if (ReceiveResult() && !intersection.empty()) {
        receiver->Add(std::move(intersection));
      }
    });

    if (recovery_manager_) {
      recovery_manager_->UpdateParsedBucketCount(bucket_idx + 1);
    }
  }
}

void PsiOperator::End() {
  SyncWait(link_ctx_, [this] { return OnEnd(); });
}

}  // namespace psi
