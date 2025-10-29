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

#include "psi/apps/psi_launcher/factory.h"

#include <memory>

#include "experiment/psi/threshold_ecdh_psi/receiver.h"
#include "experiment/psi/threshold_ecdh_psi/sender.h"
#include "experiment/psi/threshold_ub_psi/client.h"
#include "experiment/psi/threshold_ub_psi/server.h"
#include "yacl/base/exception.h"

#include "psi/algorithm/ecdh/receiver.h"
#include "psi/algorithm/ecdh/sender.h"
#include "psi/algorithm/ecdh/ub_psi/client.h"
#include "psi/algorithm/ecdh/ub_psi/server.h"
#include "psi/algorithm/kkrt/receiver.h"
#include "psi/algorithm/kkrt/sender.h"
#include "psi/algorithm/rr22/receiver.h"
#include "psi/algorithm/rr22/sender.h"

namespace psi {

std::unique_ptr<AbstractPsiParty> createPsiParty(
    const v2::PsiConfig& config, std::shared_ptr<yacl::link::Context> lctx) {
  switch (config.protocol_config().protocol()) {
    case v2::Protocol::PROTOCOL_ECDH: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          if (config.intersection_threshold() == 0) {
            return std::make_unique<ecdh::EcdhPsiReceiver>(config, lctx);
          } else {
            return std::make_unique<ecdh::ThresholdEcdhPsiReceiver>(config,
                                                                    lctx);
          }
        case v2::Role::ROLE_SENDER:
          if (config.intersection_threshold() == 0) {
            return std::make_unique<ecdh::EcdhPsiSender>(config, lctx);
          } else {
            return std::make_unique<ecdh::ThresholdEcdhPsiSender>(config, lctx);
          }
        default:
          YACL_THROW("Role is invalid.");
      }
    }
    case v2::Protocol::PROTOCOL_KKRT: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          return std::make_unique<kkrt::KkrtPsiReceiver>(config, lctx);
        case v2::Role::ROLE_SENDER:
          return std::make_unique<kkrt::KkrtPsiSender>(config, lctx);
        default:
          YACL_THROW("Role is invalid.");
      }
    }
    case v2::Protocol::PROTOCOL_RR22: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          return std::make_unique<rr22::Rr22PsiReceiver>(config, lctx);
        case v2::Role::ROLE_SENDER:
          return std::make_unique<rr22::Rr22PsiSender>(config, lctx);
        default:
          YACL_THROW("Role is invalid.");
      }
    }
    default:
      YACL_THROW("Protocol is unspecified.");
  }
}

std::unique_ptr<AbstractUbPsiParty> createUbPsiParty(
    const v2::UbPsiConfig& config, std::shared_ptr<yacl::link::Context> lctx) {
  switch (config.role()) {
    case v2::Role::ROLE_SERVER:
      if (config.intersection_threshold() == 0) {
        return std::make_unique<ecdh::EcdhUbPsiServer>(config, lctx);
      } else {
        return std::make_unique<ecdh::ThresholdEcdhUbPsiServer>(config, lctx);
      }
    case v2::Role::ROLE_CLIENT:
      if (config.intersection_threshold() == 0) {
        return std::make_unique<ecdh::EcdhUbPsiClient>(config, lctx);
      } else {
        return std::make_unique<ecdh::ThresholdEcdhUbPsiClient>(config, lctx);
      }
    default:
      YACL_THROW("Role is invalid.");
  }
}

}  // namespace psi
