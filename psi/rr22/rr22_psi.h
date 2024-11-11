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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "yacl/base/exception.h"
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

#include "psi/rr22/rr22_oprf.h"
#include "psi/utils/bucket.h"
#include "psi/utils/hash_bucket_cache.h"

// [RR22] Blazing Fast PSI from Improved OKVS and Subfield VOLE, CCS 2022
// https://eprint.iacr.org/2022/320
// okvs code reference https://github.com/Visa-Research/volepsi

namespace psi::rr22 {

struct Rr22PsiOptions {
  Rr22PsiOptions(size_t ssp_params, size_t num_threads_params,
                 bool compress_params, bool malicious_params = false)
      : ssp(ssp_params),
        num_threads(num_threads_params),
        compress(compress_params),
        malicious(malicious_params) {
    YACL_ENFORCE(ssp >= 30, "ssp:{}", ssp);

    num_threads = std::max<size_t>(1, num_threads_params);
  }

  // ssp i.e. statistical security parameter.
  // must >= 30bit
  size_t ssp = 30;

  // number of threads
  // value 0 will use 1 thread
  size_t num_threads = 0;

  // psi mode: fast or low communication
  Rr22PsiMode mode = Rr22PsiMode::FastMode;

  // wether compress the OPRF outputs
  bool compress = true;

  // run the protocol with malicious security
  // not supported by now
  bool malicious = false;

  yacl::crypto::CodeType code_type = yacl::crypto::CodeType::ExAcc7;
  const size_t oprf_bin_size = 1 << 14;
};

using PreProcessFunc =
    std::function<std::vector<HashBucketCache::BucketItem>(size_t)>;
// input: bucket index, bucket_items, intersection indices, dup cnt
using PostProcessFunc = std::function<void(
    size_t, const std::vector<HashBucketCache::BucketItem>&,
    const std::vector<uint32_t>&, const std::vector<uint32_t>&)>;

class BucketRr22Core {
 public:
  BucketRr22Core(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                 size_t bucket_idx, bool broadcast_result)
      : rr22_options_(rr22_options),
        bucket_num_(bucket_num),
        broadcast_result_(broadcast_result),
        bucket_idx_(bucket_idx) {}

  virtual void Prepare(const std::shared_ptr<yacl::link::Context>& lctx) = 0;
  virtual void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) = 0;
  virtual void GetIntersection(
      const std::shared_ptr<yacl::link::Context>& lctx) = 0;

 protected:
  Rr22PsiOptions rr22_options_;
  size_t bucket_num_;
  bool broadcast_result_;
  size_t bucket_idx_;

  size_t self_size_ = 0;
  size_t peer_size_ = 0;
  size_t mask_size_ = sizeof(uint128_t);
  std::vector<uint128_t> inputs_hash_;
  std::vector<uint128_t> oprfs_;
  std::vector<HashBucketCache::BucketItem> bucket_items_;
  bool null_bucket_ = false;
};

class BucketRr22Sender : public BucketRr22Core {
 public:
  BucketRr22Sender(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                   size_t bucket_idx, bool broadcast_result,
                   PreProcessFunc& pre_f, PostProcessFunc& post_f)
      : BucketRr22Core(rr22_options, bucket_num, bucket_idx, broadcast_result),
        pre_f_(pre_f),
        post_f_(post_f),
        oprf_sender_(rr22_options.oprf_bin_size, rr22_options_.ssp,
                     rr22_options_.mode, rr22_options_.code_type,
                     rr22_options_.malicious) {}
  void Prepare(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void GetIntersection(
      const std::shared_ptr<yacl::link::Context>& lctx) override;

 private:
  PreProcessFunc pre_f_;
  PostProcessFunc post_f_;
  Rr22OprfSender oprf_sender_;
};

class BucketRr22Receiver : public BucketRr22Core {
 public:
  BucketRr22Receiver(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                     size_t bucket_idx, bool broadcast_result,
                     PreProcessFunc& pre_f, PostProcessFunc& post_f)
      : BucketRr22Core(rr22_options, bucket_num, bucket_idx, broadcast_result),
        pre_f_(pre_f),
        post_f_(post_f),
        oprf_receiver_(rr22_options.oprf_bin_size, rr22_options_.ssp,
                       rr22_options_.mode, rr22_options_.code_type,
                       rr22_options_.malicious) {}
  void Prepare(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void GetIntersection(
      const std::shared_ptr<yacl::link::Context>& lctx) override;

 private:
  PreProcessFunc pre_f_;
  PostProcessFunc post_f_;
  Rr22OprfReceiver oprf_receiver_;
};

// return {mask_size, peer_size}
std::pair<size_t, size_t> ExchangeTruncateSize(
    const std::shared_ptr<yacl::link::Context>& lctx, size_t self_size,
    const Rr22PsiOptions& options);

class Rr22Runner {
 public:
  Rr22Runner(const std::shared_ptr<yacl::link::Context>& lctx,
             const Rr22PsiOptions& rr22_options, size_t bucket_num,
             bool broadcast_result, PreProcessFunc& pre_f,
             PostProcessFunc& post_f)
      : rr22_options_(rr22_options),
        bucket_num_(bucket_num),
        broadcast_result_(broadcast_result),
        pre_f_(pre_f),
        post_f_(post_f) {
    intersection_lctx_ = lctx->Spawn("intersection");
    read_lctx_ = lctx->Spawn("read");
    run_lctx_ = lctx->Spawn("run");
  }
  void Run(size_t start_idx, bool is_sender) {
    for (size_t idx = start_idx; idx < bucket_num_; ++idx) {
      auto bucket_runner = CreateBucketRunner(idx, is_sender);
      bucket_runner->Prepare(read_lctx_);
      bucket_runner->RunOprf(run_lctx_);
      bucket_runner->GetIntersection(intersection_lctx_);
    }
  }

  void AsyncRun(size_t start_idx, bool is_sender) {
    // cache size meaning the size you can prepare input data into queue
    // bigger cache size may run a little fast but consume more memory
    constexpr size_t cache_size = 1;
    if (bucket_num_ <= cache_size) {
      Run(start_idx, is_sender);
      return;
    }
    std::queue<std::shared_ptr<BucketRr22Core>> prepared_runner_queue;
    std::queue<std::shared_ptr<BucketRr22Core>> oprf_runner_queue;
    std::mutex prepare_mtx;
    std::condition_variable prepare_cv;
    std::mutex oprf_mtx;
    std::condition_variable oprf_cv;
    auto prepare_f = std::async(std::launch::async, [&]() {
      for (size_t i = start_idx; i < bucket_num_; i++) {
        auto runner = CreateBucketRunner(i, is_sender);
        runner->Prepare(read_lctx_);
        std::unique_lock lock(prepare_mtx);
        prepare_cv.wait(
            lock, [&] { return prepared_runner_queue.size() < cache_size; });
        prepared_runner_queue.push(runner);
        prepare_cv.notify_all();
      }
    });
    auto run_f = std::async(std::launch::async, [&]() {
      for (int i = start_idx; i < static_cast<int>(bucket_num_); ++i) {
        std::shared_ptr<BucketRr22Core> runner;
        {
          std::unique_lock lock(prepare_mtx);
          prepare_cv.wait(lock, [&] { return !prepared_runner_queue.empty(); });
          runner = prepared_runner_queue.front();
          prepared_runner_queue.pop();
          prepare_cv.notify_all();
        }
        runner->RunOprf(run_lctx_);
        {
          std::unique_lock lock(oprf_mtx);
          oprf_runner_queue.push(runner);
          oprf_cv.notify_all();
        }
      }
    });
    auto intersection_f = std::async(std::launch::async, [&]() {
      for (int i = start_idx; i < static_cast<int>(bucket_num_); ++i) {
        std::shared_ptr<BucketRr22Core> runner;
        {
          std::unique_lock lock(oprf_mtx);
          oprf_cv.wait(lock, [&] { return !oprf_runner_queue.empty(); });
          runner = oprf_runner_queue.front();
          oprf_runner_queue.pop();
          oprf_cv.notify_all();
        }
        runner->GetIntersection(intersection_lctx_);
      }
    });
    run_f.get();
    prepare_f.get();
    intersection_f.get();
  }

  void ParallelRun(size_t start_idx, bool is_sender, int parallel_num = 6) {
    if (static_cast<int>(bucket_num_) <= parallel_num) {
      Run(start_idx, is_sender);
      return;
    }
    std::vector<std::future<void>> futures(parallel_num);
    for (size_t i = 0; i < static_cast<size_t>(parallel_num); i++) {
      futures[i] = std::async(
          std::launch::async,
          [&](size_t thread_idx) {
            for (size_t j = 0; j < bucket_num_; j++) {
              if (j % parallel_num == thread_idx) {
                auto runner = CreateBucketRunner(j, is_sender);
                runner->Prepare(read_lctx_->Spawn(std::to_string(thread_idx)));
                runner->RunOprf(run_lctx_->Spawn(std::to_string(thread_idx)));
                runner->GetIntersection(
                    intersection_lctx_->Spawn(std::to_string(thread_idx)));
              }
            }
          },
          i);
    }
    for (auto& f : futures) {
      f.get();
    }
  }

 private:
  std::shared_ptr<BucketRr22Core> CreateBucketRunner(size_t idx,
                                                     bool is_sender) {
    std::shared_ptr<BucketRr22Core> bucker_runner;
    if (is_sender) {
      bucker_runner = std::make_shared<BucketRr22Sender>(
          rr22_options_, bucket_num_, idx, broadcast_result_, pre_f_, post_f_);
    } else {
      bucker_runner = std::make_shared<BucketRr22Receiver>(
          rr22_options_, bucket_num_, idx, broadcast_result_, pre_f_, post_f_);
    }
    return bucker_runner;
  }
  std::shared_ptr<yacl::link::Context> intersection_lctx_;
  std::shared_ptr<yacl::link::Context> read_lctx_;
  std::shared_ptr<yacl::link::Context> run_lctx_;
  Rr22PsiOptions rr22_options_;
  size_t bucket_num_;
  bool broadcast_result_;
  PreProcessFunc pre_f_;
  PostProcessFunc post_f_;
};

}  // namespace psi::rr22
