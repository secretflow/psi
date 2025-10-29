// Copyright 2025 Ant Group Co., Ltd.
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

#include <vector>

#include "yacl/link/link.h"

#include "psi/algorithm/psi_io.h"
#include "psi/checkpoint/recovery.h"

namespace psi {

class PsiOperator {
 public:
  struct Intersection {
    std::vector<ItemIndexType> item_index;

    // Key: item index
    // Value: the count of repetitions of peer repeating item duplication count
    std::unordered_map<ItemIndexType, ItemCntType> peer_dup_item_cnt;
  };

 public:
  explicit PsiOperator(std::shared_ptr<yacl::link::Context> link_ctx,
                       std::shared_ptr<IDataStore> input_store,
                       std::shared_ptr<IResultStore> output_store,
                       std::shared_ptr<RecoveryManager> recovery_manager,
                       bool broadcast_result);
  virtual ~PsiOperator() = default;

  virtual void Init();

  virtual void Run();

  virtual void End();

  // Whether the protocol implementation supports streaming data processing
  virtual bool SupportStreaming() = 0;

  // Whether the protocol implementation supports parallel processing of
  // multiple buckets
  virtual bool SupportBucketParallel() = 0;

  virtual bool ReceiveResult() = 0;

 protected:
  virtual void OnInit() {}

  // If the protocol implementation supports streaming data or inter-bucket
  // parallelism, implement this interface
  virtual void OnRun(/*const std::shared_ptr<IDataStore>& input_store, std::shared_ptr<IResultStore>& output_store*/) {
    YACL_THROW("Not Implemented");
  }

  // Basic version interface. This interface is used if the protocol needs to
  // run in buckets.
  virtual std::vector<PsiResultIndex> OnRun(
      const std::vector<PsiItemData>& /*inputs*/) {
    YACL_THROW("Not Implemented");
  }

  virtual void OnSequenceRun();

  virtual void OnEnd() {}

 protected:
  std::shared_ptr<yacl::link::Context> link_ctx_;

  std::shared_ptr<IDataStore> input_store_;

  std::shared_ptr<IResultStore> output_store_;

  std::shared_ptr<RecoveryManager> recovery_manager_;

  bool broadcast_result_ = false;
};

}  // namespace psi
