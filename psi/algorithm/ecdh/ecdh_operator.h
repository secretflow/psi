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

#include "psi/algorithm/ecdh/common.h"
#include "psi/algorithm/ecdh/ecdh_psi.h"
#include "psi/algorithm/psi_operator.h"

namespace psi::ecdh {

class EcdhOperator : public PsiOperator {
 public:
  explicit EcdhOperator(EcdhPsiOptions opts,
                        std::shared_ptr<IDataStore> input_store,
                        std::shared_ptr<IResultStore> output_store,
                        size_t bin_size = kDefaultBinNum,
                        std::string cache_dir = "");
  ~EcdhOperator() = default;

  bool SupportStreaming() override { return true; }

  bool SupportBucketParallel() override { return false; }

  bool ReceiveResult() override;

 protected:
  void OnInit() override;

  void OnRun() override;

  void OnEnd() override;

 private:
  EcdhPsiOptions opts_;

  size_t bin_size_;

  std::string cache_dir_;

  std::shared_ptr<IEcPointStore> self_ec_point_store_;
  std::shared_ptr<IEcPointStore> peer_ec_point_store_;
};

}  // namespace psi::ecdh
