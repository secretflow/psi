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

#include "psi/algorithm/ecdh/ub_psi/ecdh_oprf_psi.h"
#include "psi/interface.h"
#include "psi/utils/resource_manager.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::ecdh {

class EcdhUbPsiClient : public AbstractUbPsiClient {
 public:
  explicit EcdhUbPsiClient(const v2::UbPsiConfig &config,
                           std::shared_ptr<yacl::link::Context> lctx = nullptr);

  ~EcdhUbPsiClient() override = default;

  void Init() override;

  void OfflineGenCache() override;

  void OfflineTransferCache() override;

  void Online() override;

  void Offline() override;

 protected:
  std::string GetServerCachePath() const;

  EcdhOprfPsiOptions psi_options_;

  std::shared_ptr<DirResource> dir_resource_;
  std::shared_ptr<JoinProcessor> join_processor_;
};

}  // namespace psi::ecdh
