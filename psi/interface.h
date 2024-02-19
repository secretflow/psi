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

#include <cstddef>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "yacl/base/exception.h"
#include "yacl/link/algorithm/barrier.h"
#include "yacl/link/link.h"

#include "psi/utils/advanced_join.h"
#include "psi/utils/index_store.h"
#include "psi/utils/recovery.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

class AbstractPsiParty {
 public:
  AbstractPsiParty() = delete;

  AbstractPsiParty(const AbstractPsiParty &copy) = delete;

  AbstractPsiParty(AbstractPsiParty &&source) = delete;

  AbstractPsiParty(const v2::PsiConfig &config, v2::Role role,
                   std::shared_ptr<yacl::link::Context> lctx);

  PsiResultReport Run() {
    Init();

    if (!digest_equal_) {
      PreProcess();

      yacl::link::Barrier(lctx_, "psi_pre_process");

      Online();

      PostProcess();
    }

    return Finalize();
  }

  virtual ~AbstractPsiParty() = default;

 protected:
  // Including tasks independent to protocol:
  // - Config check
  // - Input check
  // - Network check
  // - Extract keys
  // - etc.
  virtual void Init();

  // Dependent to Protocol
  virtual void PreProcess() = 0;

  // Dependent to Protocol
  virtual void Online() = 0;

  // Dependent to Protocol
  virtual void PostProcess() = 0;

  // Including tasks independent to protocol:
  // - Compose output.
  // - Sort output.
  // - Write output.
  virtual PsiResultReport Finalize();

  v2::PsiConfig config_;

  v2::Role role_;

  PsiResultReport report_;

  std::vector<std::string> selected_keys_;

  std::shared_ptr<IndexWriter> intersection_indices_writer_;

  bool trunc_intersection_indices_ = false;

  std::shared_ptr<yacl::link::Context> lctx_;

  bool digest_equal_ = false;

  std::shared_ptr<RecoveryManager> recovery_manager_;

  std::string key_hash_digest_;

  std::shared_ptr<AdvancedJoinConfig> advanced_join_config_;

 private:
  void CheckPeerConfig();

  void CheckSelfConfig();
};

class AbstractPsiReceiver : public AbstractPsiParty {
 public:
  explicit AbstractPsiReceiver(const v2::PsiConfig &config,
                               std::shared_ptr<yacl::link::Context> lctx);
};

class AbstractPsiSender : public AbstractPsiParty {
 public:
  explicit AbstractPsiSender(const v2::PsiConfig &config,
                             std::shared_ptr<yacl::link::Context> lctx);
};

class AbstractUbPsiParty {
 public:
  AbstractUbPsiParty() = delete;

  AbstractUbPsiParty(const AbstractUbPsiParty &copy) = delete;

  AbstractUbPsiParty(AbstractUbPsiParty &&source) = delete;

  AbstractUbPsiParty(const v2::UbPsiConfig &config, v2::Role role,
                     std::shared_ptr<yacl::link::Context> lctx);

  PsiResultReport Run();

  virtual ~AbstractUbPsiParty() = default;

 protected:
  virtual void Init() = 0;

  virtual void OfflineGenCache() = 0;

  virtual void Offline() = 0;

  virtual void OfflineTransferCache() = 0;

  virtual void Online() = 0;

  v2::UbPsiConfig config_;

  v2::Role role_;

  std::shared_ptr<yacl::link::Context> lctx_;

  PsiResultReport report_;
};

class AbstractUbPsiServer : public AbstractUbPsiParty {
 public:
  explicit AbstractUbPsiServer(const v2::UbPsiConfig &config,
                               std::shared_ptr<yacl::link::Context> lctx);
};

class AbstractUbPsiClient : public AbstractUbPsiParty {
 public:
  explicit AbstractUbPsiClient(const v2::UbPsiConfig &config,
                               std::shared_ptr<yacl::link::Context> lctx);
};

}  // namespace psi
