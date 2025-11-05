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
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yacl/base/exception.h"
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

#include "psi/algorithm/rr22/rr22_oprf.h"
#include "psi/utils/bucket.h"
#include "psi/utils/hash_bucket_cache.h"
#include "psi/utils/simple_channel.h"

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

class BucketRr22Core {
 public:
  BucketRr22Core(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                 size_t bucket_idx, bool broadcast_result,
                 IBucketDataStore* data_processor)
      : rr22_options_(rr22_options),
        bucket_num_(bucket_num),
        broadcast_result_(broadcast_result),
        bucket_idx_(bucket_idx),
        data_processor_(data_processor) {}

  virtual ~BucketRr22Core() = default;
  virtual void Vole(const std::shared_ptr<yacl::link::Context>& lctx,
                    bool cache_vole = false,
                    const std::filesystem::path& cache_dir =
                        std::filesystem::temp_directory_path() /
                        GetRandomString()) = 0;
  virtual void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) = 0;
  virtual void Intersection(
      const std::shared_ptr<yacl::link::Context>& lctx) = 0;
  virtual void BroadCastResult(
      const std::shared_ptr<yacl::link::Context>& lctx) = 0;
  virtual bool IsSender() = 0;
  virtual void WriteResult() = 0;
  [[nodiscard]] size_t BucketIdx() const { return bucket_idx_; }

 protected:
  Rr22PsiOptions rr22_options_;
  size_t bucket_num_;
  bool broadcast_result_;
  size_t bucket_idx_;

  size_t self_size_ = 0;
  size_t peer_size_ = 0;
  size_t vole_size_ = 0;
  size_t mask_size_ = sizeof(uint128_t);
  std::vector<uint128_t> inputs_hash_;
  std::vector<uint128_t> oprfs_;
  std::vector<HashBucketCache::BucketItem> bucket_items_;
  bool null_bucket_ = false;
  IBucketDataStore* data_processor_;
};

class BucketRr22Sender : public BucketRr22Core {
 public:
  BucketRr22Sender(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                   size_t bucket_idx, bool broadcast_result,
                   IBucketDataStore* data_processor)
      : BucketRr22Core(rr22_options, bucket_num, bucket_idx, broadcast_result,
                       data_processor),
        oprf_sender_(rr22_options.oprf_bin_size, rr22_options_.ssp,
                     rr22_options_.mode, rr22_options_.code_type,
                     rr22_options_.malicious) {}
  void Vole(const std::shared_ptr<yacl::link::Context>& lctx,
            bool cache_vole = false,
            const std::filesystem::path& cache_dir =
                std::filesystem::temp_directory_path() /
                GetRandomString()) override;
  void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void Intersection(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void BroadCastResult(
      const std::shared_ptr<yacl::link::Context>& lctx) override;
  void WriteResult() override;
  bool IsSender() override { return true; }

 private:
  Rr22OprfSender oprf_sender_;
  std::vector<uint32_t> indices_;
  std::vector<uint32_t> peer_cnt_;
};

class BucketRr22Receiver : public BucketRr22Core {
 public:
  BucketRr22Receiver(const Rr22PsiOptions& rr22_options, size_t bucket_num,
                     size_t bucket_idx, bool broadcast_result,
                     IBucketDataStore* data_processor)
      : BucketRr22Core(rr22_options, bucket_num, bucket_idx, broadcast_result,
                       data_processor),
        oprf_receiver_(rr22_options.oprf_bin_size, rr22_options_.ssp,
                       rr22_options_.mode, rr22_options_.code_type,
                       rr22_options_.malicious) {}
  void Vole(const std::shared_ptr<yacl::link::Context>& lctx,
            bool cache_vole = false,
            const std::filesystem::path& cache_dir =
                std::filesystem::temp_directory_path() /
                GetRandomString()) override;
  void RunOprf(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void Intersection(const std::shared_ptr<yacl::link::Context>& lctx) override;
  void BroadCastResult(
      const std::shared_ptr<yacl::link::Context>& lctx) override;
  void WriteResult() override;
  bool IsSender() override { return false; }

 private:
  Rr22OprfReceiver oprf_receiver_;
  std::vector<uint32_t> self_cnt_;
  std::vector<uint32_t> peer_cnt_;
  std::vector<uint32_t> self_indices_;
  std::vector<uint32_t> peer_indices_;
};

class Rr22Runner {
 public:
  Rr22Runner(const std::shared_ptr<yacl::link::Context>& lctx,
             const Rr22PsiOptions& rr22_options, size_t bucket_num,
             bool broadcast_result, IBucketDataStore* data_processor)
      : lctx_(lctx),
        rr22_options_(rr22_options),
        bucket_num_(bucket_num),
        broadcast_result_(broadcast_result),
        data_processor_(data_processor) {}
  void Run(size_t start_idx, bool is_sender) {
    for (size_t idx = start_idx; idx < bucket_num_; ++idx) {
      auto bucket_runner = CreateBucketRunner(idx, is_sender);
      bucket_runner->Vole(lctx_);
      bucket_runner->RunOprf(lctx_);
      bucket_runner->Intersection(lctx_);
      bucket_runner->BroadCastResult(lctx_);
      bucket_runner->WriteResult();
    }
  }

  void AsyncRun(size_t start_idx, bool is_sender,
                const std::filesystem::path& cache_dir) {
    // cache size meaning the size you can prepare input data into queue
    // bigger cache size may run a little fast but consume more memory
    constexpr size_t cache_size = 2;
    if (bucket_num_ <= cache_size) {
      Run(start_idx, is_sender);
      return;
    }
    // create cache dir if not exist
    if (!std::filesystem::exists(cache_dir)) {
      std::filesystem::create_directory(cache_dir);
    }
    auto helper =
        [&](SimpleChannel<std::shared_ptr<BucketRr22Core>>* run_queue,
            SimpleChannel<std::shared_ptr<BucketRr22Core>>* result_queue,
            size_t capacity) {
          SimpleChannel<std::shared_ptr<BucketRr22Core>> intersection_queue(
              capacity);
          SimpleChannel<std::shared_ptr<BucketRr22Core>> broadcast_queue(
              capacity);
          auto run_f = std::async(std::launch::async, [&]() {
            while (true) {
              auto data = run_queue->Pop();
              if (!data.has_value()) {
                break;
              }
              auto runner = data.value();
              std::shared_ptr<yacl::link::Context> lctx =
                  lctx_->Spawn("oprf-" + std::to_string(runner->BucketIdx()));
              runner->RunOprf(lctx);
              intersection_queue.Push(runner);
            }
            intersection_queue.Close();
          });
          auto intersection_f = std::async(std::launch::async, [&]() {
            while (true) {
              auto data = intersection_queue.Pop();
              if (!data.has_value()) {
                break;
              }
              auto runner = data.value();
              std::shared_ptr<yacl::link::Context> lctx = lctx_->Spawn(
                  "intersection-" + std::to_string(runner->BucketIdx()));
              runner->Intersection(lctx);
              broadcast_queue.Push(runner);
            }
            broadcast_queue.Close();
          });
          auto broadcast_f = std::async(std::launch::async, [&]() {
            while (true) {
              auto data = broadcast_queue.Pop();
              if (!data.has_value()) {
                break;
              }
              auto runner = data.value();
              std::shared_ptr<yacl::link::Context> lctx = lctx_->Spawn(
                  "broadcasrt-" + std::to_string(runner->BucketIdx()));
              runner->BroadCastResult(lctx);
              result_queue->Push(runner);
            }
            result_queue->Close();
          });
          intersection_f.get();
          broadcast_f.get();
        };
    // create vole in parallel
    std::vector<std::shared_ptr<BucketRr22Core>> runners(bucket_num_);
    // selected based on test results
    constexpr size_t VoleParallelSize = 6;
    std::vector<std::future<void>> futures(VoleParallelSize);
    for (size_t i = 0; i < futures.size(); i++) {
      futures[i] = std::async(
          std::launch::async,
          [&](size_t thread_idx) {
            for (size_t j = 0; j < bucket_num_; j++) {
              if (j % futures.size() == thread_idx) {
                SPDLOG_INFO("idx: {}, is_sender: {}", j, is_sender);
                auto runner = CreateBucketRunner(j, is_sender);
                std::shared_ptr<yacl::link::Context> spawn_lctx =
                    lctx_->Spawn("vole-" + std::to_string(runner->BucketIdx()));
                runner->Vole(spawn_lctx, true, cache_dir);
                runners[runner->BucketIdx()] = runner;
              }
            }
          },
          i);
    }
    for (auto& f : futures) {
      f.get();
    }
    SimpleChannel<std::shared_ptr<BucketRr22Core>> run_queue(bucket_num_);
    for (size_t idx = start_idx; idx < bucket_num_; idx++) {
      run_queue.Push(runners[idx]);
    }
    runners.clear();
    run_queue.Close();
    SimpleChannel<std::shared_ptr<BucketRr22Core>> result_queue(cache_size);
    auto f = std::async(std::launch::async, helper, &run_queue, &result_queue,
                        cache_size);

    for (size_t idx = start_idx; idx < bucket_num_; idx++) {
      auto data = result_queue.Pop();
      data.value()->WriteResult();
    }
    f.get();
  }

  // deprecated
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
            std::shared_ptr<yacl::link::Context> spawn_lctx =
                lctx_->Spawn(std::to_string(thread_idx));
            for (size_t j = 0; j < bucket_num_; j++) {
              if (j % parallel_num == thread_idx) {
                auto runner = CreateBucketRunner(j, is_sender);
                runner->Vole(spawn_lctx);
                runner->RunOprf(spawn_lctx);
                runner->Intersection(spawn_lctx);
                runner->BroadCastResult(spawn_lctx);
                runner->WriteResult();
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
          rr22_options_, bucket_num_, idx, broadcast_result_, data_processor_);
    } else {
      bucker_runner = std::make_shared<BucketRr22Receiver>(
          rr22_options_, bucket_num_, idx, broadcast_result_, data_processor_);
    }
    return bucker_runner;
  }

  std::shared_ptr<yacl::link::Context> lctx_;
  Rr22PsiOptions rr22_options_;
  size_t bucket_num_;
  bool broadcast_result_;
  IBucketDataStore* data_processor_;
};

size_t ComputeMaskSize(const Rr22PsiOptions& options, size_t self_size,
                       size_t peer_size);
}  // namespace psi::rr22
