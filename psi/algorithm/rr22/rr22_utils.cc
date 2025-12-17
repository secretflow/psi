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

#include "psi/algorithm/rr22/rr22_utils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "libdivide.h"
#include "sparsehash/dense_hash_map"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/rr22/okvs/galois128.h"
#include "psi/algorithm/rr22/okvs/simple_index.h"
#include "psi/utils/serialize.h"

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

std::pair<std::vector<uint32_t>, std::vector<uint32_t>> GetIntersectionReceiver(
    const std::vector<uint128_t>& self_oprfs,
    const std::vector<HashBucketCache::BucketItem>& self_items,
    size_t peer_items_num, const std::shared_ptr<yacl::link::Context>& lctx,
    size_t num_threads, size_t mask_size, bool broadcast_result) {
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
  auto peer_buffer = lctx->Recv(lctx->NextRank(), fmt::format("paxos_solve"));
  YACL_ENFORCE(peer_items_num == peer_buffer.size() / mask_size);
  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_items_num);
  auto cnt_buffer = lctx->Recv(lctx->NextRank(), fmt::format("items_cnt"));
  auto peer_item_cnt_map = utils::DeserializeItemsCnt(cnt_buffer);
  for (uint32_t i = 0; i < peer_items_num; ++i) {
    (void)peer_item_cnt_map[i];
  }

  map_f.get();
  auto* peer_data_ptr = peer_buffer.data<uint8_t>();
  std::mutex merge_mtx;
  std::vector<uint32_t> self_indices;
  std::vector<uint32_t> peer_cnt;
  std::vector<uint32_t> peer_indices;
  std::vector<uint32_t> self_cnt;
  size_t grain_size = (peer_items_num + num_threads - 1) / num_threads;
  yacl::parallel_for(
      0, peer_items_num, grain_size, [&](int64_t begin, int64_t end) {
        std::vector<uint32_t> tmp_indexs;
        std::vector<uint32_t> tmp_peer_cnt;

        std::vector<uint32_t> tmp_peer_indexs;
        std::vector<uint32_t> tmp_self_cnt;

        uint128_t data = yacl::MakeUint128(0, 0);
        for (int64_t j = begin; j < end; j++) {
          std::memcpy(&data, peer_data_ptr + (j * mask_size), mask_size);
          auto iter = dense_map.find(data);
          if (iter != dense_map.end()) {
            tmp_indexs.push_back(iter->second);
            tmp_peer_cnt.push_back(peer_item_cnt_map[j]);
            if (broadcast_result) {
              tmp_peer_indexs.push_back(j);
              YACL_ENFORCE(
                  iter->second < self_items.size(),
                  "random str matched in result, which is not expected.");
              tmp_self_cnt.push_back(self_items[iter->second].extra_dup_cnt);
            }
          }
        }
        if (!tmp_indexs.empty()) {
          std::lock_guard<std::mutex> lock(merge_mtx);
          self_indices.insert(self_indices.end(), tmp_indexs.begin(),
                              tmp_indexs.end());
          peer_cnt.insert(peer_cnt.end(), tmp_peer_cnt.begin(),
                          tmp_peer_cnt.end());
          if (broadcast_result) {
            peer_indices.insert(peer_indices.end(), tmp_peer_indexs.begin(),
                                tmp_peer_indexs.end());
            self_cnt.insert(self_cnt.end(), tmp_self_cnt.begin(),
                            tmp_self_cnt.end());
          }
        }
      });
  if (broadcast_result) {
    auto buffer = yacl::Buffer(peer_indices.data(),
                               peer_indices.size() * sizeof(uint32_t));
    lctx->SendAsyncThrottled(lctx->NextRank(), buffer, "broadcast_result");
    std::unordered_map<uint64_t, uint32_t> self_cnt_map;
    for (size_t i = 0; i < self_cnt.size(); ++i) {
      if (self_cnt[i] > 0) {
        self_cnt_map[i] = self_cnt[i];
      }
    }
    lctx->SendAsyncThrottled(lctx->NextRank(),
                             utils::SerializeItemsCnt(self_cnt_map),
                             "broadcast_items_cnt");
  }
  return {self_indices, peer_cnt};
}

std::vector<uint32_t> GetIntersectionReceiver(
    std::vector<uint128_t> self_oprfs, size_t peer_items_num,
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
  yacl::Buffer peer_buffer(peer_items_num * mask_size);
  size_t recv_item_count = 0;
  while (recv_item_count < peer_items_num) {
    auto buffer = lctx->Recv(lctx->NextRank(), fmt::format("oprf_value"));
    std::memcpy(peer_buffer.data<uint8_t>() + (recv_item_count * mask_size),
                buffer.data<uint8_t>(), buffer.size());
    recv_item_count += buffer.size() / mask_size;
  }
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

std::pair<std::vector<uint32_t>, std::vector<uint32_t>> GetIntersectionSender(
    std::vector<uint128_t> self_oprfs,
    const std::vector<HashBucketCache::BucketItem>& items,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t mask_size,
    bool broadcast_result) {
  std::vector<uint32_t> result;
  bool compress = mask_size != sizeof(uint128_t);
  auto* data_ptr = reinterpret_cast<std::byte*>(self_oprfs.data());
  if (compress) {
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      std::memmove(data_ptr + (i * mask_size), &self_oprfs[i], mask_size);
    }
  }
  yacl::ByteContainerView send_buffer(data_ptr, self_oprfs.size() * mask_size);
  lctx->SendAsyncThrottled(lctx->NextRank(), send_buffer,
                           fmt::format("oprf_value"));
  std::unordered_map<uint64_t, uint32_t> self_cnt;
  for (size_t i = 0; i != items.size(); ++i) {
    if (items[i].extra_dup_cnt > 0) {
      self_cnt[i] = items[i].extra_dup_cnt;
    }
  }
  lctx->SendAsyncThrottled(lctx->NextRank(), utils::SerializeItemsCnt(self_cnt),
                           fmt::format("items_cnt"));
  std::unordered_map<uint64_t, uint32_t> items_cnt;
  if (broadcast_result) {
    auto buffer = lctx->Recv(lctx->NextRank(), "broadcast_result");
    result.resize(buffer.size() / sizeof(uint32_t));
    std::memcpy(result.data(), buffer.data<uint8_t>(), buffer.size());
    buffer = lctx->Recv(lctx->NextRank(), "broadcast_items_cnt");
    items_cnt = utils::DeserializeItemsCnt(buffer);
  }
  std::vector<uint32_t> items_cnt_v(result.size(), 0);
  for (auto& item : items_cnt) {
    items_cnt_v[item.first] = item.second;
  }
  return {result, items_cnt_v};
}

std::vector<uint32_t> GetIntersectionSender(
    std::vector<uint128_t> self_oprfs,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t mask_size,
    bool broadcast_result) {
  std::vector<uint32_t> result;
  bool compress = mask_size != sizeof(uint128_t);
  auto* data_ptr = reinterpret_cast<std::byte*>(self_oprfs.data());
  if (compress) {
    for (size_t i = 0; i < self_oprfs.size(); ++i) {
      std::memmove(data_ptr + (i * mask_size), &self_oprfs[i], mask_size);
    }
  }
  for (size_t i = 0; i < self_oprfs.size(); i += kSendChunkSize) {
    yacl::ByteContainerView send_buffer(
        data_ptr + (i * mask_size),
        std::min<size_t>(kSendChunkSize, self_oprfs.size() - i) * mask_size);
    lctx->Send(lctx->NextRank(), send_buffer, fmt::format("send oprf_buf"));
  }
  if (broadcast_result) {
    auto buffer = lctx->Recv(lctx->NextRank(), "broadcast_result");
    result.resize(buffer.size() / sizeof(uint32_t));
    std::memcpy(result.data(), buffer.data<uint8_t>(), buffer.size());
  }
  return result;
}

}  // namespace psi::rr22
