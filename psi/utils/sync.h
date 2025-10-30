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

#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "yacl/link/link.h"

#include "psi/algorithm/types.h"

#include "psi/utils/serializable.pb.h"

namespace psi {

namespace {

const std::string kFinishedFlag = "p_finished";
const std::string kUnFinishedFlag = "p_unfinished";

}  // namespace

template <typename F, typename... Args>
auto SyncWait(const std::shared_ptr<yacl::link::Context>& lctx, F&& func,
              Args&&... args) {
  using R = std::invoke_result_t<F, Args...>;

  std::shared_ptr<yacl::link::Context> sync_lctx = lctx->Spawn();
  std::vector<yacl::Buffer> flag_list;
  std::chrono::seconds span(5);
  bool done = false;

  auto fut = std::async(std::launch::async, std::forward<F>(func),
                        std::forward<Args>(args)...);

  std::conditional_t<!std::is_void_v<R>, R, std::monostate> res{};

  while (true) {
    if (!done) {
      auto status = fut.wait_for(span);
      if (status == std::future_status::ready) {
        done = true;
        try {
          if constexpr (!std::is_void_v<R>) {
            // gets the result only if the return value is not void
            res = fut.get();
          } else {
            fut.get();
          }
        } catch (...) {
          // TODO: Consider sending msg to notify the other party to voluntarily
          // quit
          std::rethrow_exception(std::current_exception());
        }
      }
    }

    // sync stage
    auto flag = done ? kFinishedFlag : kUnFinishedFlag;
    flag_list = yacl::link::AllGather(sync_lctx, flag, "sync_wait");
    if (std::none_of(flag_list.begin(), flag_list.end(),
                     [](const yacl::Buffer& b) {
                       return std::string_view(b.data<char>(), b.size()) ==
                              kUnFinishedFlag;
                     })) {
      break;
    }
  }

  if constexpr (!std::is_void_v<R>) {
    return res;
  }
}

std::vector<size_t> AllGatherItemsSize(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t self_size);

void BroadcastResult(const std::shared_ptr<yacl::link::Context>& link_ctx,
                     std::vector<std::string>* res);

void BroadcastResult(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    std::vector<std::string>* res,
    std::unordered_map<ItemIndexType, ItemCntType>* res_dup_cnt);

std::vector<bool> AllGatherFlag(
    const std::shared_ptr<yacl::link::Context>& link_ctx, bool self_flag);

}  // namespace psi
