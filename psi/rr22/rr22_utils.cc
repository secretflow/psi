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
#include <future>
#include <utility>

#include "absl/types/span.h"
#include "libdivide.h"
#include "sparsehash/dense_hash_map"

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
    bool compress, size_t mask_size, size_t ssp) {
  num_threads = std::max<size_t>(1, num_threads);

  if (self_oprfs.size() < num_threads) {
    num_threads = self_oprfs.size();
  }

  int128_t truncate_mask = yacl::MakeUint128(0, 0);
  for (size_t i = 0; i < mask_size; ++i) {
    truncate_mask = 0xff | (truncate_mask << 8);
    SPDLOG_DEBUG(
        "{}, truncate_mask:{}", i,
        (std::ostringstream() << okvs::Galois128(truncate_mask)).str());
  }

  auto divider = libdivide::libdivide_u32_gen(num_threads);

  std::vector<std::thread> thrds(num_threads);
  std::mutex merge_mtx;

  size_t bin_size = okvs::SimpleIndex::GetBinSize(
      num_threads, std::max(self_oprfs.size(), peer_items_num), ssp);

  SPDLOG_INFO("bin_size:{} {}?={} compress:{} mask_size:{}", bin_size,
              self_oprfs.size(), peer_items_num, compress, mask_size);

  std::atomic<uint64_t> hash_done = 0;
  std::promise<void> sync_prom;
  std::promise<void> hash_done_promise;

  std::shared_future<void> hashing_done_future =
      hash_done_promise.get_future().share();

  std::shared_future<void> hash_sync_future = sync_prom.get_future().share();

  std::vector<size_t> indices;

  yacl::Buffer peer_buffer(peer_items_num * mask_size);
  static const size_t batch_size = 128;

  auto proc = [&](size_t thread_idx) -> void {
    google::dense_hash_map<uint128_t, size_t, NoHash> dense_map(bin_size);
    dense_map.set_empty_key(yacl::MakeUint128(0, 0));

    std::array<std::pair<uint128_t, size_t>, batch_size> hh;

    for (size_t i = 0; i < self_oprfs.size();) {
      size_t j = 0;
      while ((j != batch_size) && i < self_oprfs.size()) {
        uint32_t v = 0;
        if (num_threads > 1) {
          std::memcpy(&v, &self_oprfs[i], sizeof(uint32_t));
          auto k = libdivide::libdivide_u32_do(v, &divider);
          v -= k * num_threads;
        }
        if (v == thread_idx) {
          if (compress) {
            hh[j] = {self_oprfs[i] & truncate_mask, i};
          } else {
            hh[j] = {self_oprfs[i], i};
          }
          ++j;
        }
        ++i;
      }
      dense_map.insert(hh.begin(), hh.begin() + j);
    }
    if (++hash_done == num_threads) {
      hash_done_promise.set_value();
    } else {
      hashing_done_future.get();
    }

    hash_sync_future.get();

    SPDLOG_DEBUG("thread_idx map size:{}", dense_map.size());
    SPDLOG_DEBUG("peer_buffer size:{}", peer_buffer.size());

    if (dense_map.size() == 0) {
      return;
    }

    size_t intersection_size = 0;
    size_t begin = thread_idx * self_oprfs.size() / num_threads;
    size_t* intersection = (size_t*)(&self_oprfs[begin]);

    uint8_t* peer_data_ptr = (uint8_t*)(peer_buffer.data());

    uint128_t h = yacl::MakeUint128(0, 0);

    for (size_t i = 0; i < peer_items_num; ++i) {
      std::memcpy(&h, peer_data_ptr, mask_size);
      peer_data_ptr += mask_size;

      uint32_t v = 0;
      if (num_threads > 1) {
        std::memcpy(&v, &h, sizeof(uint32_t));
        auto k = libdivide::libdivide_u32_do(v, &divider);
        v -= k * num_threads;
      }

      if (v == thread_idx) {
        auto iter = dense_map.find(h);
        if (iter != dense_map.end()) {
          intersection[intersection_size] = iter->second;

          ++intersection_size;
        }
      }
    }

    if (intersection_size) {
      std::lock_guard<std::mutex> lock(merge_mtx);
      indices.insert(indices.end(), intersection,
                     intersection + intersection_size);
    }
  };

  for (size_t i = 0; i < num_threads; ++i) {
    thrds[i] = std::thread(proc, i);
  }

  SPDLOG_INFO("recv rr22 oprf begin");

  size_t recv_count = 0;
  while (true) {
    auto recv_buffer =
        lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));

    std::memcpy((uint8_t*)peer_buffer.data() + (recv_count * mask_size),
                recv_buffer.data(), recv_buffer.size());
    recv_count += recv_buffer.size() / mask_size;
    if (recv_count == peer_items_num) {
      break;
    }
  }

  sync_prom.set_value();

  SPDLOG_INFO("recv rr22 oprf finished: {} vector:{}", peer_buffer.size(),
              peer_buffer.size() / mask_size);

  for (size_t i = 0; i < num_threads; ++i) {
    thrds[i].join();
  }

  return indices;
}

}  // namespace psi::rr22
