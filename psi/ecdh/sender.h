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

#include "psi/ecdh/ecdh_psi.h"
#include "psi/interface.h"
#include "psi/utils/arrow_csv_batch_provider.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::ecdh {

class EcdhPsiSender final : public AbstractPsiSender {
 public:
  explicit EcdhPsiSender(const v2::PsiConfig &config,
                         std::shared_ptr<yacl::link::Context> lctx = nullptr);

  ~EcdhPsiSender() override = default;

 private:
  void Init() override;

  void PreProcess() override;

  void Online() override;

  void PostProcess() override;

  EcdhPsiOptions psi_options_;

  std::shared_ptr<ArrowCsvBatchProvider> batch_provider_;

  std::shared_ptr<HashBucketEcPointStore> self_ec_point_store_;
  std::shared_ptr<HashBucketEcPointStore> peer_ec_point_store_;
};

}  // namespace psi::ecdh
