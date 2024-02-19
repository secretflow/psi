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

#include "psi/utils/serializable.pb.h"

namespace psi {

namespace {

static const std::string kFinishedFlag = "p_finished";
static const std::string kUnFinishedFlag = "p_unfinished";

}  // namespace

template <typename T>
typename std::enable_if_t<std::is_void_v<T>> SyncWait(
    const std::shared_ptr<yacl::link::Context>& lctx, std::future<T>* f) {
  std::shared_ptr<yacl::link::Context> sync_lctx = lctx->Spawn();
  std::vector<yacl::Buffer> flag_list;
  std::chrono::seconds span(5);
  bool done = false;

  while (true) {
    if (!done) {
      done = f->wait_for(span) == std::future_status::ready;
      if (done) {
        // check if exception is raised.
        try {
          f->get();
        } catch (...) {
          std::exception_ptr ep = std::current_exception();
          std::rethrow_exception(ep);
        }
      }
    }
    auto flag = done ? kFinishedFlag : kUnFinishedFlag;
    flag_list = yacl::link::AllGather(sync_lctx, flag, "sync wait");
    if (std::find_if(flag_list.begin(), flag_list.end(),
                     [](const yacl::Buffer& b) {
                       return std::string_view(b.data<char>(), b.size()) ==
                              kUnFinishedFlag;
                     }) == flag_list.end()) {
      // all done
      break;
    }
  }
}

template <typename T>
typename std::enable_if_t<!std::is_void_v<T>, T> SyncWait(
    const std::shared_ptr<yacl::link::Context>& lctx, std::future<T>* f) {
  std::shared_ptr<yacl::link::Context> sync_lctx = lctx->Spawn();
  std::vector<yacl::Buffer> flag_list;
  std::chrono::seconds span(5);
  bool done = false;
  T res{};

  while (true) {
    if (!done) {
      done = f->wait_for(span) == std::future_status::ready;
      if (done) {
        // check if exception is raised.
        try {
          res = f->get();
        } catch (...) {
          std::exception_ptr ep = std::current_exception();
          std::rethrow_exception(ep);
        }
      }
    }
    auto flag = done ? kFinishedFlag : kUnFinishedFlag;
    flag_list = yacl::link::AllGather(sync_lctx, flag, "sync wait");
    if (std::find_if(flag_list.begin(), flag_list.end(),
                     [](const yacl::Buffer& b) {
                       return std::string_view(b.data<char>(), b.size()) ==
                              kUnFinishedFlag;
                     }) == flag_list.end()) {
      // all done
      break;
    }
  }

  return res;
}

std::vector<size_t> AllGatherItemsSize(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t self_size);

void BroadcastResult(const std::shared_ptr<yacl::link::Context>& link_ctx,
                     std::vector<std::string>* res);

}  // namespace psi
