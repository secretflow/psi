// Copyright 2025 The secretflow authors.
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

#include "psi/algorithm/ypir/params.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::ypir {

psi::spiral::Params CreateParamsForScenarioSimplePIR(
    uint64_t /*num_items*/, uint64_t /*item_size_bits*/) {
  // For now, use the fast expansion testing params as a reasonable default
  // In production, you would calculate optimal parameters based on num_items
  // and item_size_bits
  return psi::spiral::util::GetFastExpansionTestingParam();
}

psi::spiral::Params CreateParamsForScenario(uint64_t /*num_items*/,
                                            uint64_t /*item_size_bits*/) {
  // For now, use the fast expansion testing params for DoublePIR too
  // In production, you would use different parameters optimized for DoublePIR
  return psi::spiral::util::GetFastExpansionTestingParam();
}

}  // namespace psi::ypir
