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

#include "psi/psi/factory.h"

#include <memory>

#include "yacl/base/exception.h"

#include "psi/psi/ecdh/receiver.h"
#include "psi/psi/ecdh/sender.h"
#include "psi/psi/kkrt/receiver.h"
#include "psi/psi/kkrt/sender.h"
#include "psi/psi/rr22/receiver.h"
#include "psi/psi/rr22/sender.h"

namespace psi::psi {

std::unique_ptr<AbstractPSIParty> createPSIParty(
    const v2::PsiConfig& config, std::shared_ptr<yacl::link::Context> lctx) {
  switch (config.protocol_config().protocol()) {
    case v2::Protocol::PROTOCOL_ECDH: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          return std::make_unique<ecdh::EcdhPSIReceiver>(config, lctx);
        case v2::Role::ROLE_SENDER:
          return std::make_unique<ecdh::EcdhPSISender>(config, lctx);
        default:
          YACL_THROW("Role is unspecified.");
      }
    }
    case v2::Protocol::PROTOCOL_KKRT: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          return std::make_unique<kkrt::KkrtPSIReceiver>(config, lctx);
        case v2::Role::ROLE_SENDER:
          return std::make_unique<kkrt::KkrtPSISender>(config, lctx);
        default:
          YACL_THROW("Role is unspecified.");
      }
    }
    case v2::Protocol::PROTOCOL_RR22: {
      switch (config.protocol_config().role()) {
        case v2::Role::ROLE_RECEIVER:
          return std::make_unique<rr22::Rr22PSIReceiver>(config, lctx);
        case v2::Role::ROLE_SENDER:
          return std::make_unique<rr22::Rr22PSISender>(config, lctx);
        default:
          YACL_THROW("Role is unspecified.");
      }
    }
    default:
      YACL_THROW("Protocol is unspecified.");
  }
}

}  // namespace psi::psi
