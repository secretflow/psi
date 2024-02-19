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

#include <memory>
#include <vector>

#include "yacl/base/buffer.h"
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

namespace psi::rr22 {

std::vector<size_t> GetIntersection(
    absl::Span<const uint128_t> self_oprfs, size_t peer_items_num,
    const std::shared_ptr<yacl::link::Context>& lctx, size_t num_threads,
    bool compress, size_t mask_size, size_t ssp = 40);
}
