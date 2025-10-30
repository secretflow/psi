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

#include "psi/algorithm/ecdh/ecdh_operator.h"

#include <memory>
#include <unordered_map>

#include "psi/algorithm/ecdh/common.h"
#include "psi/utils/ec_point_store.h"

namespace psi::ecdh {

namespace {

// TODO: drop BasicBatchProvider, directly use IDataProvider
class ProviderWrapper : public IBasicBatchProvider {
 public:
  explicit ProviderWrapper(std::shared_ptr<IDataProvider> provider,
                           size_t batch_size)
      : provider_(std::move(provider)), batch_size_(batch_size) {}

  std::vector<std::string> ReadNextBatch() override {
    auto item_datas = provider_->ReadNext(batch_size_);
    std::vector<std::string> res(item_datas.size());
    for (size_t i = 0; i != res.size(); ++i) {
      res[i] = std::move(item_datas[i].buf);
    }
    return res;
  }

  std::pair<std::vector<std::string>, std::unordered_map<uint32_t, uint32_t>>
  ReadNextBatchWithDupCnt() override {
    auto item_datas = provider_->ReadNext(batch_size_);
    std::vector<std::string> res(item_datas.size());
    std::unordered_map<uint32_t, uint32_t> dup_map;
    for (size_t i = 0; i != res.size(); ++i) {
      res[i] = std::move(item_datas[i].buf);
      if (item_datas[i].cnt > 1) {
        dup_map.emplace(i, item_datas[i].cnt - 1);
      }
    }
    return {res, dup_map};
  }

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  std::shared_ptr<IDataProvider> provider_;
  size_t batch_size_;
};

}  // namespace

EcdhOperator::EcdhOperator(EcdhPsiOptions opts,
                           std::shared_ptr<IDataStore> input_store,
                           std::shared_ptr<IResultStore> output_store,
                           size_t bin_size, std::string cache_dir)
    : PsiOperator(opts.link_ctx, std::move(input_store),
                  std::move(output_store), opts.recovery_manager, false),
      opts_(std::move(opts)),
      bin_size_(bin_size),
      cache_dir_(std::move(cache_dir)) {}

bool EcdhOperator::ReceiveResult() {
  return opts_.target_rank == yacl::link::kAllRank ||
         opts_.target_rank == opts_.link_ctx->Rank();
}

void EcdhOperator::OnInit() {
  if (recovery_manager_) {
    self_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        recovery_manager_->ecdh_dual_masked_self_cache_path(), kDefaultBinNum,
        false);
    peer_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
        recovery_manager_->ecdh_dual_masked_peer_cache_path(), kDefaultBinNum,
        false);
    recovery_manager_->MarkPreProcessEnd(opts_.ecc_cryptor);
  } else {
    if (bin_size_ > 1) {
      self_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
          std::filesystem::path(cache_dir_) / "self_ec_point_store",
          kDefaultBinNum);
      peer_ec_point_store_ = std::make_shared<HashBucketEcPointStore>(
          std::filesystem::path(cache_dir_) / "peer_ec_point_store",
          kDefaultBinNum);
    } else {
      self_ec_point_store_ = std::make_shared<MemoryEcPointStore>();
      peer_ec_point_store_ = std::make_shared<MemoryEcPointStore>();
    }
  }
}

void EcdhOperator::OnRun() {
  auto provider = input_store_->Load(0);
  auto wrapper =
      std::make_shared<ProviderWrapper>(std::move(provider), opts_.batch_size);

  RunEcdhPsi(opts_, wrapper, self_ec_point_store_, peer_ec_point_store_);
}

void EcdhOperator::OnEnd() {
  auto recevier = output_store_->GetReceiver(0);

  if (bin_size_ > 1) {
    auto self_ec_point_store =
        std::dynamic_pointer_cast<HashBucketEcPointStore>(self_ec_point_store_);
    auto peer_ec_point_store =
        std::dynamic_pointer_cast<HashBucketEcPointStore>(peer_ec_point_store_);
    YACL_ENFORCE(self_ec_point_store);
    YACL_ENFORCE(peer_ec_point_store);

    YACL_ENFORCE_EQ(self_ec_point_store->num_bins(),
                    peer_ec_point_store->num_bins());
    self_ec_point_store->Flush();
    peer_ec_point_store->Flush();

    // Compute indices
    for (size_t bin_idx = 0; bin_idx != self_ec_point_store->num_bins();
         ++bin_idx) {
      std::vector<HashBucketCache::BucketItem> self_results =
          self_ec_point_store->LoadBucketItems(bin_idx);
      std::vector<HashBucketCache::BucketItem> peer_results =
          peer_ec_point_store->LoadBucketItems(bin_idx);

      std::unordered_set<HashBucketCache::BucketItem,
                         HashBucketCache::HashBucketIter>
          peer_set(peer_results.begin(), peer_results.end());
      for (const auto& item : self_results) {
        auto peer = peer_set.find(item);
        if (peer != peer_set.end()) {
          recevier->Add(PsiResultIndex{
              .data = item.index, .peer_item_cnt = peer->extra_dup_cnt + 1});
        }
      }
    }
  } else {
    auto self_ec_point_store =
        std::dynamic_pointer_cast<MemoryEcPointStore>(self_ec_point_store_);
    auto peer_ec_point_store =
        std::dynamic_pointer_cast<MemoryEcPointStore>(peer_ec_point_store_);
    YACL_ENFORCE(self_ec_point_store);
    YACL_ENFORCE(peer_ec_point_store);

    std::vector<std::string> ret;
    std::unordered_map<std::string, uint32_t> peer_ciphertext_map;
    auto& peer_results = peer_ec_point_store->content();
    for (uint32_t i = 0; i != peer_results.size(); ++i) {
      peer_ciphertext_map.emplace(std::move(peer_results[i]), i);
    }
    const auto& peer_dup_cnt_map = peer_ec_point_store->GetDupMap();
    const auto& self_results = self_ec_point_store->content();
    for (uint32_t index = 0; index < self_results.size(); index++) {
      auto iter = peer_ciphertext_map.find(self_results[index]);
      if (iter != peer_ciphertext_map.end()) {
        uint32_t peer_cnt = 1;
        auto cnt_iter = peer_dup_cnt_map.find(iter->second);
        if (cnt_iter != peer_dup_cnt_map.end()) {
          peer_cnt += cnt_iter->second;
        }
        recevier->Add(PsiResultIndex{.data = index, .peer_item_cnt = peer_cnt});
      }
    }
  }
  recevier->Finish();
}

}  // namespace psi::ecdh
