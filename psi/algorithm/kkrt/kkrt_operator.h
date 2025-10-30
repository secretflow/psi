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

#include "psi/algorithm/kkrt/kkrt_psi.h"
#include "psi/algorithm/psi_operator.h"

namespace psi::kkrt {

class KkrtOperator : public PsiOperator {
 public:
  struct Options {
    std::shared_ptr<yacl::link::Context> link_ctx;
    size_t receiver_rank;
    bool broadcast_result = false;
    KkrtPsiOptions psi_opts = GetDefaultKkrtPsiOptions();
  };

  explicit KkrtOperator(Options opts, std::shared_ptr<IDataStore> input_store,
                        std::shared_ptr<IResultStore> output_store);
  ~KkrtOperator() = default;

  bool SupportStreaming() override { return false; }

  bool SupportBucketParallel() override { return false; }

  bool ReceiveResult() override;

 protected:
  void OnInit() override;

  std::vector<PsiResultIndex> OnRun(
      const std::vector<PsiItemData>& inputs) override;

  void OnEnd() override;

 private:
  Options opts_;

  bool is_recevicer_;

  std::unique_ptr<yacl::crypto::OtRecvStore> ot_recv_;
  std::unique_ptr<yacl::crypto::OtSendStore> ot_send_;
};

}  // namespace psi::kkrt
