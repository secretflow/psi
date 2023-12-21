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

#include "psi/psi/recovery.h"
#include "psi/psi/utils/advanced_join.h"
#include "psi/psi/utils/index_store.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::psi {

class AbstractPSIParty {
 public:
  AbstractPSIParty() = delete;

  AbstractPSIParty(const AbstractPSIParty &copy) = delete;

  AbstractPSIParty(AbstractPSIParty &&source) = delete;

  AbstractPSIParty(const v2::PsiConfig &config, v2::Role role,
                   std::shared_ptr<yacl::link::Context> lctx = nullptr);

  // AbstractPSIParty(const v2::PsiConfig &config, v2::Role role);

  v2::PsiReport Run() {
    Init();

    if (!digest_equal_) {
      PreProcess();

      yacl::link::Barrier(lctx_, "psi_pre_process");

      Online();

      PostProcess();
    }

    return Finalize();
  }

  virtual ~AbstractPSIParty() = default;

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
  virtual v2::PsiReport Finalize();

  v2::PsiConfig config_;

  v2::Role role_;

  v2::PsiReport report_;

  std::vector<std::string> selected_keys_;

  std::shared_ptr<IndexWriter> intersection_indices_writer_;

  bool trunc_intersection_indices_ = false;

  std::shared_ptr<yacl::link::Context> lctx_;

  bool digest_equal_ = false;

  std::shared_ptr<RecoveryManager> recovery_manager_;

  std::string key_hash_digest_;

  std::shared_ptr<AdvancedJoinConfig> advanced_join_config_;

  bool need_intersection_deduplication_ = false;

 private:
  void CheckPeerConfig();

  void CheckSelfConfig();
};

class AbstractPSIReceiver : public AbstractPSIParty {
 public:
  explicit AbstractPSIReceiver(
      const v2::PsiConfig &config,
      std::shared_ptr<yacl::link::Context> lctx = nullptr);
};

class AbstractPSISender : public AbstractPSIParty {
 public:
  explicit AbstractPSISender(
      const v2::PsiConfig &config,
      std::shared_ptr<yacl::link::Context> lctx = nullptr);
};

}  // namespace psi::psi
