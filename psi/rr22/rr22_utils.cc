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

#include "psi/rr22/rr22_utils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "libdivide.h"
#include "sparsehash/dense_hash_map"
#include "yacl/utils/parallel.h"

#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/okvs/simple_index.h"

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

std::vector<size_t> GetIntersection(
    absl::Span<const uint128_t> self_oprfs, size_t peer_items_num,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t num_threads,
    bool compress, size_t mask_size) {
  if (!compress) {
    mask_size = sizeof(uint128_t);
  }
  google::dense_hash_map<uint128_t, size_t, NoHash> dense_map(
      self_oprfs.size());
  dense_map.set_empty_key(yacl::MakeUint128(0, 0));
  auto map_f = std::async([&]() {
    auto truncate_mask = yacl::MakeUint128(0, 0);
    if (compress) {
      for (size_t i = 0; i < mask_size; ++i) {
        truncate_mask = 0xff | (truncate_mask << 8);
        SPDLOG_DEBUG(
            "{}, truncate_mask:{}", i,
            (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
      }
    }
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      if (compress) {
        dense_map.insert(std::make_pair(self_oprfs[i] & truncate_mask, i));
      } else {
        dense_map.insert(std::make_pair(self_oprfs[i], i));
      }
    }
  });
  SPDLOG_INFO("recv rr22 oprf begin");
  auto peer_buffer =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));
  YACL_ENFORCE(peer_items_num == peer_buffer.size() / mask_size);
  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_items_num);
  map_f.get();
  auto* peer_data_ptr = peer_buffer.data<uint8_t>();
  std::mutex merge_mtx;
  std::vector<size_t> indices;
  size_t grain_size = (peer_items_num + num_threads - 1) / num_threads;
  yacl::parallel_for(
      0, peer_items_num, grain_size, [&](int64_t begin, int64_t end) {
        std::vector<uint32_t> tmp_indexs;
        uint128_t data = yacl::MakeUint128(0, 0);
        for (int64_t j = begin; j < end; j++) {
          std::memcpy(&data, peer_data_ptr + (j * mask_size), mask_size);
          auto iter = dense_map.find(data);
          if (iter != dense_map.end()) {
            tmp_indexs.push_back(iter->second);
          }
        }
        if (!tmp_indexs.empty()) {
          std::lock_guard<std::mutex> lock(merge_mtx);
          indices.insert(indices.end(), tmp_indexs.begin(), tmp_indexs.end());
        }
      });
  return indices;
}

std::vector<uint32_t> GetIntersectionReceiver(
    const std::vector<uint128_t>& self_oprfs, size_t peer_items_num,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t num_threads,
    size_t mask_size, bool broadcast_result) {
  bool compress = mask_size != sizeof(uint128_t);
  google::dense_hash_map<uint128_t, size_t, NoHash> dense_map(
      self_oprfs.size());
  dense_map.set_empty_key(yacl::MakeUint128(0, 0));
  auto map_f = std::async([&]() {
    auto truncate_mask = yacl::MakeUint128(0, 0);
    if (compress) {
      for (size_t i = 0; i < mask_size; ++i) {
        truncate_mask = 0xff | (truncate_mask << 8);
        SPDLOG_DEBUG(
            "{}, truncate_mask:{}", i,
            (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
      }
    }
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      if (compress) {
        dense_map.insert(std::make_pair(self_oprfs[i] & truncate_mask, i));
      } else {
        dense_map.insert(std::make_pair(self_oprfs[i], i));
      }
    }
  });
  SPDLOG_INFO("recv rr22 oprf begin");
  auto peer_buffer =
      lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));
  YACL_ENFORCE(peer_items_num == peer_buffer.size() / mask_size);
  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_items_num);
  map_f.get();
  auto* peer_data_ptr = peer_buffer.data<uint8_t>();
  std::mutex merge_mtx;
  std::vector<uint32_t> self_indices;
  std::vector<uint32_t> peer_indices;
  size_t grain_size = (peer_items_num + num_threads - 1) / num_threads;
  yacl::parallel_for(
      0, peer_items_num, grain_size, [&](int64_t begin, int64_t end) {
        std::vector<uint32_t> tmp_indexs;
        std::vector<uint32_t> tmp_peer_indexs;
        uint128_t data = yacl::MakeUint128(0, 0);
        for (int64_t j = begin; j < end; j++) {
          std::memcpy(&data, peer_data_ptr + (j * mask_size), mask_size);
          auto iter = dense_map.find(data);
          if (iter != dense_map.end()) {
            tmp_indexs.push_back(iter->second);
            if (broadcast_result) {
              tmp_peer_indexs.push_back(j);
            }
          }
        }
        if (!tmp_indexs.empty()) {
          std::lock_guard<std::mutex> lock(merge_mtx);
          self_indices.insert(self_indices.end(), tmp_indexs.begin(),
                              tmp_indexs.end());
          if (broadcast_result) {
            peer_indices.insert(peer_indices.end(), tmp_peer_indexs.begin(),
                                tmp_peer_indexs.end());
          }
        }
      });
  if (broadcast_result) {
    auto buffer = yacl::Buffer(peer_indices.data(),
                               peer_indices.size() * sizeof(uint32_t));
    lctx->SendAsyncThrottled(lctx->NextRank(), buffer, "broadcast_result");
  }
  return self_indices;
}

std::vector<uint32_t> GetIntersectionSender(
    std::vector<uint128_t> self_oprfs,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t mask_size,
    bool broadcast_result) {
  std::vector<uint32_t> result;
  bool compress = mask_size != sizeof(uint128_t);
  auto truncate_mask = yacl::MakeUint128(0, 0);
  auto* data_ptr = reinterpret_cast<std::byte*>(self_oprfs.data());
  if (compress) {
    for (size_t i = 0; i < mask_size; ++i) {
      truncate_mask = 0xff | (truncate_mask << 8);
      SPDLOG_DEBUG(
          "{}, truncate_mask:{}", i,
          (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
    }
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      std::memcpy(data_ptr + (i * mask_size), &self_oprfs[i], mask_size);
    }
  }
  yacl::ByteContainerView send_buffer(data_ptr, self_oprfs.size() * mask_size);
  lctx->SendAsyncThrottled(lctx->NextRank(), send_buffer,
                           fmt::format("send oprf_buf"));
  if (broadcast_result) {
    auto buffer = lctx->Recv(lctx->NextRank(), "broadcast_result");
    result.resize(buffer.size() / sizeof(uint32_t));
    std::memcpy(result.data(), buffer.data<uint8_t>(), buffer.size());
  }
  return result;
}

}  // namespace psi::rr22
