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

#include <memory>
#include <vector>

#include "yacl/base/buffer.h"
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

#include "psi/utils/bucket.h"

namespace psi::rr22 {

constexpr size_t kSendChunkSize = 100000;

std::vector<uint32_t> GetIntersectionReceiver(
    std::vector<uint128_t> self_oprfs, size_t peer_items_num,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t num_threads,
    size_t mask_size, bool broadcast_result);

std::vector<uint32_t> GetIntersectionSender(
    std::vector<uint128_t> self_oprfs,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t mask_size,
    bool broadcast_result);

// indexes of intersection, peer_dup_cnt of each index
std::pair<std::vector<uint32_t>, std::vector<uint32_t>> GetIntersectionSender(
    std::vector<uint128_t> self_oprfs,
    const std::vector<HashBucketCache::BucketItem>& items,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t mask_size,
    bool broadcast_result);

// indexes of intersection, peer_dup_cnt of each index
std::pair<std::vector<uint32_t>, std::vector<uint32_t>> GetIntersectionReceiver(
    const std::vector<uint128_t>& self_oprfs,
    const std::vector<HashBucketCache::BucketItem>& self_items,
    size_t peer_items_num, const std::shared_ptr<yacl::link::Context>& lctx,
    size_t num_threads, size_t mask_size, bool broadcast_result);

template <typename ValueType>
std::vector<ValueType> RecvChunked(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t paxos_size) {
  std::vector<ValueType> paxos_solve_v(paxos_size, 0);
  size_t recv_item_count = 0;

  while (recv_item_count < paxos_size) {
    yacl::Buffer paxos_solve_buf =
        lctx->Recv(lctx->NextRank(), fmt::format("recv paxos_solve"));

    std::memcpy(paxos_solve_v.data() + recv_item_count, paxos_solve_buf.data(),
                paxos_solve_buf.size());

    recv_item_count += paxos_solve_buf.size() / sizeof(ValueType);
  }
  YACL_ENFORCE(recv_item_count == paxos_size);
  return paxos_solve_v;
}

template <typename ValueType>
void SendChunked(const std::shared_ptr<yacl::link::Context>& lctx,
                 absl::Span<ValueType> paxos_solve) {
  for (size_t i = 0; i < paxos_solve.size(); i += kSendChunkSize) {
    size_t batch_size =
        std::min<size_t>(kSendChunkSize, paxos_solve.size() - i);
    yacl::ByteContainerView paxos_solve_byteview(
        &paxos_solve[i], batch_size * sizeof(ValueType));

    lctx->Send(lctx->NextRank(), paxos_solve_byteview,
               fmt::format("send paxos_solve_byteview"));
  }
}

}  // namespace psi::rr22