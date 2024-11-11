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

#include "psi/rr22/rr22_psi.h"

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

#include "yacl/base/byte_container_view.h"
#include "yacl/utils/parallel.h"

#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/rr22_oprf.h"
#include "psi/rr22/rr22_utils.h"
#include "psi/utils/bucket.h"
#include "psi/utils/sync.h"

namespace psi::rr22 {

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

std::pair<size_t, size_t> ExchangeTruncateSize(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t self_size,
    const Rr22PsiOptions& options) {
  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(lctx, self_size);

  YACL_ENFORCE(self_size == items_size[lctx->Rank()]);
  size_t peer_size = items_size[lctx->NextRank()];
  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(self_size, peer_size, options.ssp,
                                    options.malicious);
  }
  return {mask_size, peer_size};
}

void BucketRr22Sender::Prepare(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  bucket_items_ = pre_f_(bucket_idx_);
  self_size_ = bucket_items_.size();
  std::mt19937 g(yacl::crypto::SecureRandU64());
  std::shuffle(bucket_items_.begin(), bucket_items_.end(), g);

  self_size_ = bucket_items_.size();
  std::tie(mask_size_, peer_size_) =
      ExchangeTruncateSize(lctx, bucket_items_.size(), rr22_options_);
  SPDLOG_INFO("mask size: {}", mask_size_);
  if ((peer_size_ == 0) || (self_size_ == 0)) {
    null_bucket_ = true;
    return;
  }

  inputs_hash_ = std::vector<uint128_t>(bucket_items_.size());
  yacl::parallel_for(0, bucket_items_.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      inputs_hash_[i] = yacl::crypto::Blake3_128(bucket_items_[i].base64_data);
    }
  });
  oprf_sender_.Init(lctx, std::max(self_size_, peer_size_),
                    rr22_options_.num_threads);
}

void BucketRr22Sender::RunOprf(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    return;
  }
  auto inputs_hash = oprf_sender_.Send(lctx, inputs_hash_);
  oprfs_ = oprf_sender_.Eval(inputs_hash_, inputs_hash);
}

void BucketRr22Sender::GetIntersection(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    post_f_(bucket_idx_, bucket_items_, {}, {});
    return;
  }
  SPDLOG_INFO("get intersection begin");
  std::vector<uint32_t> indices;
  std::vector<uint32_t> peer_cnt;
  std::tie(indices, peer_cnt) = GetIntersectionSender(
      oprfs_, bucket_items_, lctx, mask_size_, broadcast_result_);
  SPDLOG_INFO("get intersection end");
  post_f_(bucket_idx_, bucket_items_, indices, peer_cnt);
  SPDLOG_INFO("get intersection post f");
}

void BucketRr22Receiver::Prepare(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  bucket_items_ = pre_f_(bucket_idx_);

  self_size_ = bucket_items_.size();
  std::tie(mask_size_, peer_size_) =
      ExchangeTruncateSize(lctx, bucket_items_.size(), rr22_options_);
  SPDLOG_INFO("mask size: {}", mask_size_);
  if ((peer_size_ == 0) || (self_size_ == 0)) {
    null_bucket_ = true;
    return;
  }

  inputs_hash_ = std::vector<uint128_t>(std::max(peer_size_, self_size_));
  yacl::parallel_for(0, bucket_items_.size(), [&](int64_t begin, int64_t end) {
    for (int64_t i = begin; i < end; ++i) {
      inputs_hash_[i] = yacl::crypto::Blake3_128(bucket_items_[i].base64_data);
    }
  });
  if (peer_size_ > self_size_) {
    for (size_t idx = self_size_; idx < peer_size_; idx++) {
      inputs_hash_[idx] = yacl::crypto::SecureRandU128();
    }
  }
  oprf_receiver_.Init(lctx, inputs_hash_.size(), rr22_options_.num_threads);
}

void BucketRr22Receiver::RunOprf(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    return;
  }
  oprfs_ = oprf_receiver_.Recv(lctx, inputs_hash_);
}

void BucketRr22Receiver::GetIntersection(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  if (null_bucket_) {
    post_f_(bucket_idx_, bucket_items_, {}, {});
    return;
  }
  SPDLOG_INFO("get intersection begin");
  std::vector<uint32_t> indices;
  std::vector<uint32_t> peer_cnt;
  std::tie(indices, peer_cnt) = GetIntersectionReceiver(
      oprfs_, bucket_items_, peer_size_, lctx, rr22_options_.num_threads,
      mask_size_, broadcast_result_);
  SPDLOG_INFO("get intersection end");
  post_f_(bucket_idx_, bucket_items_, indices, peer_cnt);
  SPDLOG_INFO("get intersection post f");
}
}  // namespace psi::rr22
