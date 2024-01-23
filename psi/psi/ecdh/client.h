// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/psi/core/ecdh_oprf_psi.h"
#include "psi/psi/interface.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::psi::ecdh {

class EcdhUbPsiClient final : public AbstractUbPsiClient {
 public:
  explicit EcdhUbPsiClient(const v2::UbPsiConfig &config,
                           std::shared_ptr<yacl::link::Context> lctx = nullptr);

  ~EcdhUbPsiClient() override = default;

  void Init() override;

  void OfflineGenCache() override;

  void OfflineTransferCache() override;

  void Online() override;

  void Offline() override;

 private:
  EcdhOprfPsiOptions psi_options_;
};

}  // namespace psi::psi::ecdh
