// Copyright 2022 Ant Group Co., Ltd.
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

#include "psi/utils/sync.h"

#include <unordered_map>

#include "psi/utils/serialize.h"

namespace psi {

std::vector<bool> AllGatherFlag(
    const std::shared_ptr<yacl::link::Context>& link_ctx, bool self_flag) {
  std::vector<bool> flag_list(link_ctx->WorldSize());
  uint8_t int_flage = self_flag ? 1 : 0;
  std::vector<yacl::Buffer> buffer_list =
      yacl::link::AllGather(link_ctx, {&int_flage, 1}, "PSI:SYNC_FLAG");
  for (size_t idx = 0; idx < buffer_list.size(); idx++) {
    flag_list[idx] = (buffer_list[idx].data<uint8_t>()[0] == 1);
  }
  return flag_list;
}

std::vector<size_t> AllGatherItemsSize(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t self_size) {
  std::vector<size_t> items_size_list(link_ctx->WorldSize());

  std::vector<yacl::Buffer> items_size_buf_list = yacl::link::AllGather(
      link_ctx, utils::SerializeSize(self_size), "PSI:SYNC_SIZE");

  for (size_t idx = 0; idx < items_size_buf_list.size(); idx++) {
    items_size_list[idx] = utils::DeserializeSize(items_size_buf_list[idx]);
  }

  return items_size_list;
}

void BroadcastResult(const std::shared_ptr<yacl::link::Context>& link_ctx,
                     std::vector<std::string>* res) {
  std::unordered_map<uint32_t, uint32_t> res_dup_cnt;
  BroadcastResult(link_ctx, res, &res_dup_cnt);
}

void BroadcastResult(const std::shared_ptr<yacl::link::Context>& link_ctx,
                     std::vector<std::string>* res,
                     std::unordered_map<uint32_t, uint32_t>* res_dup_cnt) {
  size_t max_size = res->size();
  size_t broadcast_rank = 0;
  std::vector<size_t> res_size_list = AllGatherItemsSize(link_ctx, res->size());
  for (size_t i = 0; i < res_size_list.size(); ++i) {
    max_size = std::max(max_size, res_size_list[i]);
    if (res_size_list[i] > 0) {
      // in broadcast case, there should be only one party have results
      YACL_ENFORCE(broadcast_rank == 0);
      broadcast_rank = i;
    }
  }
  if (max_size == 0) {
    // no need broadcast
    return;
  }
  auto recv_res_buf = yacl::link::Broadcast(
      link_ctx, utils::SerializeStrItems(*res, *res_dup_cnt), broadcast_rank,
      "broadcast psi result");
  if (res->empty()) {
    // use broadcast result
    utils::DeserializeStrItems(recv_res_buf, res, res_dup_cnt);
  }
}

}  // namespace psi
