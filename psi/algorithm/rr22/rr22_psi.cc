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

#include "psi/algorithm/rr22/rr22_psi.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "sparsehash/dense_hash_map"
#include "yacl/base/byte_container_view.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/rr22/okvs/galois128.h"
#include "psi/algorithm/rr22/rr22_oprf.h"
#include "psi/algorithm/rr22/rr22_utils.h"
#include "psi/utils/bucket.h"
#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi::rr22 {

namespace {

struct NoHash {
  inline size_t operator()(const uint128_t& v) const {
    uint32_t v32;
    std::memcpy(&v32, &v, sizeof(uint32_t));

    return v32;
  }
};

}  // namespace

namespace {

size_t ComputeTruncateSize(size_t self_size, size_t peer_size, size_t ssp,
                           bool malicious) {
  size_t truncate_size =
      malicious
          ? sizeof(uint128_t)
          : std::min<size_t>(
                std::ceil((ssp + std::ceil(std::log2(self_size * peer_size))) /
                          8),
                sizeof(uint128_t));

  return truncate_size;
}

}  // namespace

size_t ComputeMaskSize(const Rr22PsiOptions& options, size_t self_size,
                       size_t peer_size) {
  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(self_size, peer_size, options.ssp,
                                    options.malicious);
  }
  return mask_size;
}

void BucketRr22Sender::Vole(const std::shared_ptr<yacl::link::Context>& lctx,
                            bool cache_vole,
                            const std::filesystem::path& cache_dir) {
  std::tie(self_size_, peer_size_) = data_processor_->GetBucketDatasize(bucket_idx_);
  mask_size_ = ComputeMaskSize(rr22_options_, self_size_, peer_size_);
  SPDLOG_INFO("mask size: {}", mask_size_);
  if ((peer_size_ == 0) || (self_size_ == 0)) {
    null_bucket_ = true;
    return;
  }
  oprf_sender_.Init(lctx, peer_size_, rr22_options_.num_threads, cache_vole,
                    cache_dir);
}

void BucketRr22Sender::RunOprf(const std::shared_ptr<yacl::link::Context>&) {
  if (null_bucket_) {
    return;
  }
  bucket_items_ = data_processor_->GetBucketItems(bucket_idx_);
  std::mt19937 g(yacl::crypto::SecureRandU64());
  std::shuffle(bucket_items_.begin(), bucket_items_.end(), g);

  inputs_hash_ = std::vector<uint128_t>(bucket_items_.size());
  yacl::parallel_for(0, bucket_items_.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      inputs_hash_[i] = yacl::crypto::Blake3_128(bucket_items_[i].base64_data);
    }
  });
}

void BucketRr22Sender::Intersection(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    data_processor_->WriteIntersetionItems(bucket_idx_, bucket_items_, {}, {});
    return;
  }
  auto inputs_hash = oprf_sender_.Send(lctx, inputs_hash_);
  oprfs_ = oprf_sender_.Eval(inputs_hash_, inputs_hash);
  SPDLOG_INFO("get intersection begin");
  std::vector<uint32_t> result;
  bool compress = mask_size_ != sizeof(uint128_t);
  auto truncate_mask = yacl::MakeUint128(0, 0);
  auto* data_ptr = reinterpret_cast<std::byte*>(oprfs_.data());
  if (compress) {
    for (size_t i = 0; i < mask_size_; ++i) {
      truncate_mask = 0xff | (truncate_mask << 8);
      SPDLOG_DEBUG(
          "{}, truncate_mask:{}", i,
          (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
    }
    for (size_t i = 0; i < oprfs_.size(); ++i) {
      std::memcpy(data_ptr + (i * mask_size_), &oprfs_[i], mask_size_);
    }
  }
  yacl::ByteContainerView send_buffer(data_ptr, oprfs_.size() * mask_size_);
  lctx->SendAsyncThrottled(lctx->NextRank(), send_buffer,
                           fmt::format("oprf_value"));
  std::unordered_map<uint64_t, uint32_t> self_cnt;
  for (size_t i = 0; i != bucket_items_.size(); ++i) {
    if (bucket_items_[i].extra_dup_cnt > 0) {
      self_cnt[i] = bucket_items_[i].extra_dup_cnt;
    }
  }
  lctx->SendAsyncThrottled(lctx->NextRank(), utils::SerializeItemsCnt(self_cnt),
                           fmt::format("items_cnt"));
  SPDLOG_INFO("get intersection end");
}

void BucketRr22Sender::BroadCastResult(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  std::unordered_map<uint64_t, uint32_t> items_cnt;
  if (broadcast_result_) {
    auto buffer = lctx->Recv(lctx->NextRank(), "broadcast_result");
    indices_.resize(buffer.size() / sizeof(uint32_t));
    std::memcpy(indices_.data(), buffer.data<uint8_t>(), buffer.size());
    buffer = lctx->Recv(lctx->NextRank(), "broadcast_items_cnt");
    items_cnt = utils::DeserializeItemsCnt(buffer);
  }
  peer_cnt_.resize(indices_.size());
  for (auto& item : items_cnt) {
    peer_cnt_[item.first] = item.second;
  }
}

void BucketRr22Sender::WriteResult() {
  data_processor_->WriteIntersetionItems(bucket_idx_, bucket_items_, indices_, peer_cnt_);
  SPDLOG_INFO("sender write bucket idx {} result", bucket_idx_);
}

void BucketRr22Receiver::Vole(const std::shared_ptr<yacl::link::Context>& lctx,
                              bool cache_vole,
                              const std::filesystem::path& cache_dir) {
  std::tie(self_size_, peer_size_) = data_processor_->GetBucketDatasize(bucket_idx_);
  mask_size_ = ComputeMaskSize(rr22_options_, self_size_, peer_size_);
  SPDLOG_INFO("mask size: {}", mask_size_);
  if ((peer_size_ == 0) || (self_size_ == 0)) {
    null_bucket_ = true;
    return;
  }
  oprf_receiver_.Init(lctx, self_size_, rr22_options_.num_threads, cache_vole,
                      cache_dir);
}

void BucketRr22Receiver::RunOprf(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    return;
  }

  bucket_items_ = data_processor_->GetBucketItems(bucket_idx_);

  inputs_hash_ = std::vector<uint128_t>(bucket_items_.size());
  yacl::parallel_for(0, bucket_items_.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      inputs_hash_[i] = yacl::crypto::Blake3_128(bucket_items_[i].base64_data);
    }
  });
  oprfs_ = oprf_receiver_.Recv(lctx, inputs_hash_);
}

void BucketRr22Receiver::Intersection(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    data_processor_->WriteIntersetionItems(bucket_idx_, bucket_items_, {}, {});
    return;
  }
  SPDLOG_INFO("get intersection begin");
  bool compress = mask_size_ != sizeof(uint128_t);
  google::dense_hash_map<uint128_t, size_t, NoHash> dense_map(oprfs_.size());
  dense_map.set_empty_key(yacl::MakeUint128(0, 0));
  auto map_f = std::async([&]() {
    auto truncate_mask = yacl::MakeUint128(0, 0);
    if (compress) {
      for (size_t i = 0; i < mask_size_; ++i) {
        truncate_mask = 0xff | (truncate_mask << 8);
        SPDLOG_DEBUG(
            "{}, truncate_mask:{}", i,
            (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
      }
    }
    for (size_t i = 0; i < oprfs_.size(); ++i) {
      if (compress) {
        dense_map.insert(std::make_pair(oprfs_[i] & truncate_mask, i));
      } else {
        dense_map.insert(std::make_pair(oprfs_[i], i));
      }
    }
  });
  SPDLOG_INFO("recv rr22 oprf begin");
  auto peer_buffer = lctx->Recv(lctx->NextRank(), fmt::format("paxos_solve"));
  YACL_ENFORCE(peer_size_ == peer_buffer.size() / mask_size_);
  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_size_);
  auto cnt_buffer = lctx->Recv(lctx->NextRank(), fmt::format("items_cnt"));
  auto peer_item_cnt_map = utils::DeserializeItemsCnt(cnt_buffer);
  for (uint32_t i = 0; i < peer_size_; ++i) {
    (void)peer_item_cnt_map[i];
  }

  map_f.get();
  auto* peer_data_ptr = peer_buffer.data<uint8_t>();
  std::mutex merge_mtx;
  size_t grain_size =
      (peer_size_ + rr22_options_.num_threads - 1) / rr22_options_.num_threads;
  yacl::parallel_for(
      0, peer_size_, grain_size, [&](int64_t begin, int64_t end) {
        std::vector<uint32_t> tmp_indexs;
        std::vector<uint32_t> tmp_peer_cnt;

        std::vector<uint32_t> tmp_peer_indexs;
        std::vector<uint32_t> tmp_self_cnt;

        uint128_t data = yacl::MakeUint128(0, 0);
        for (int64_t j = begin; j < end; j++) {
          std::memcpy(&data, peer_data_ptr + (j * mask_size_), mask_size_);
          auto iter = dense_map.find(data);
          if (iter != dense_map.end()) {
            tmp_indexs.push_back(iter->second);
            tmp_peer_cnt.push_back(peer_item_cnt_map[j]);
            if (broadcast_result_) {
              tmp_peer_indexs.push_back(j);
              YACL_ENFORCE(
                  iter->second < bucket_items_.size(),
                  "random str matched in result, which is not expected.");
              tmp_self_cnt.push_back(bucket_items_[iter->second].extra_dup_cnt);
            }
          }
        }
        if (!tmp_indexs.empty()) {
          std::lock_guard<std::mutex> lock(merge_mtx);
          self_indices_.insert(self_indices_.end(), tmp_indexs.begin(),
                               tmp_indexs.end());
          peer_cnt_.insert(peer_cnt_.end(), tmp_peer_cnt.begin(),
                           tmp_peer_cnt.end());
          if (broadcast_result_) {
            peer_indices_.insert(peer_indices_.end(), tmp_peer_indexs.begin(),
                                 tmp_peer_indexs.end());
            self_cnt_.insert(self_cnt_.end(), tmp_self_cnt.begin(),
                             tmp_self_cnt.end());
          }
        }
      });
  SPDLOG_INFO("get intersection end");
}

void BucketRr22Receiver::BroadCastResult(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (broadcast_result_) {
    auto buffer = yacl::Buffer(peer_indices_.data(),
                               peer_indices_.size() * sizeof(uint32_t));
    lctx->SendAsyncThrottled(lctx->NextRank(), buffer, "broadcast_result");
    std::unordered_map<uint64_t, uint32_t> self_cnt_map;
    for (size_t i = 0; i < self_cnt_.size(); ++i) {
      if (self_cnt_[i] > 0) {
        self_cnt_map[i] = self_cnt_[i];
      }
    }
    lctx->SendAsyncThrottled(lctx->NextRank(),
                             utils::SerializeItemsCnt(self_cnt_map),
                             "broadcast_items_cnt");
  }
}

void BucketRr22Receiver::WriteResult() {
  data_processor_->WriteIntersetionItems(bucket_idx_, bucket_items_, self_indices_, peer_cnt_);
  SPDLOG_INFO("receiver write bucket idx {} result", bucket_idx_);
}

}  // namespace psi::rr22
