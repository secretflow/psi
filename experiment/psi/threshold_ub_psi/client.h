// Copyright 2025
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

#include "psi/algorithm/ecdh/ub_psi/client.h"

namespace psi::ecdh {
class ThresholdEcdhUbPsiClient : public EcdhUbPsiClient {
 public:
  explicit ThresholdEcdhUbPsiClient(
      const v2::UbPsiConfig &config,
      std::shared_ptr<yacl::link::Context> lctx = nullptr);

  void Online() override;

 private:
  void ResizeIntersection(IntersectionIndexInfo &intersection_info,
                          uint32_t final_count);
};
}  // namespace psi::ecdh