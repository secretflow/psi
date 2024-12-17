// Copyright 2024 zhangwfjh
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

#include <memory>
#include <tuple>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/link/link.h"

namespace psi::psi {

// Table-based OPPRF, see https://eprint.iacr.org/2017/799.pdf (Figure 6)

std::vector<uint64_t> KmprtOpprfRecv(
    const std::shared_ptr<yacl::link::Context>&,
    const std::vector<uint128_t>& queries);

void KmprtOpprfSend(const std::shared_ptr<yacl::link::Context>&,
                    const std::vector<uint128_t>& xs,
                    const std::vector<uint64_t>& ys);

}  // namespace psi::psi
