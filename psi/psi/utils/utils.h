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
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "yacl/link/link.h"

#include "psi/psi/utils/serializable.pb.h"

namespace psi::psi {

namespace {

static const std::string kFinishedFlag = "p_finished";
static const std::string kUnFinishedFlag = "p_unfinished";

}  // namespace

// Multiple-Key out-of-core sort.
// Out-of-core support reference:
//   http://vkundeti.blogspot.com/2008/03/tech-algorithmic-details-of-unix-sort.html
// Multiple-Key support reference:
//   https://stackoverflow.com/questions/9471101/sort-csv-file-by-column-priority-using-the-sort-command
// use POSIX locale for sort
//   https://unix.stackexchange.com/questions/43465/whats-the-default-order-of-linux-sort/43466
//
// NOTE:
// This implementation requires `sort` command, which is guaranteed by our
// docker-way ship.
void MultiKeySort(const std::string& in_csv, const std::string& out_csv,
                  const std::vector<std::string>& keys,
                  bool numeric_sort = false, bool unique = false);

// NOTE(junfeng): `indices` must be sorted
size_t FilterFileByIndices(const std::string& input, const std::string& output,
                           const std::vector<uint64_t>& indices,
                           bool output_difference,
                           size_t header_line_count = 1);

size_t FilterFileByIndices(const std::string& input, const std::string& output,
                           const std::filesystem::path& indices,
                           bool output_difference,
                           size_t header_line_count = 1);

// join keys with ","
std::string KeysJoin(const std::vector<absl::string_view>& keys);

std::vector<size_t> AllGatherItemsSize(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t self_size);

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

void BroadcastResult(const std::shared_ptr<yacl::link::Context>& link_ctx,
                     std::vector<std::string>* res);

std::vector<size_t> GetShuffledIdx(size_t items_size);

std::vector<uint8_t> PaddingData(yacl::ByteContainerView data, size_t max_len);
std::string UnPaddingData(yacl::ByteContainerView data);

class Limiter {
 private:
  std::mutex mtx;
  std::condition_variable cv;
  size_t count;
  const size_t max_count;

 public:
  Limiter(size_t max_count) : count(0), max_count(max_count) {}

  void acquire() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]() { return count < max_count; });
    ++count;
  }

  void release() {
    std::unique_lock<std::mutex> lock(mtx);
    --count;
    lock.unlock();
    cv.notify_one();
  }

  void wait() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]() { return count == 0; });
  }
};

#define PsiParallelFor(begin, end, parallelism, ...)               \
  do {                                                             \
    Limiter limiter(parallelism);                                  \
    for (; begin < end; begin++) {                                 \
      if (parallelism == 1)                                        \
        __VA_ARGS__(begin);                                        \
      else {                                                       \
        std::shared_ptr<yacl::link::Context> ctx = lctx_->Spawn(); \
        limiter.acquire();                                         \
        std::thread th([&, begin, lctx_ = ctx]() {                 \
          __VA_ARGS__(begin);                                      \
          limiter.release();                                       \
        });                                                        \
        th.detach();                                               \
      }                                                            \
    }                                                              \
    limiter.wait();                                                \
  } while (false);

}  // namespace psi::psi
