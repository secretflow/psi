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

#pragma once

#include <memory>
#include <vector>

#include "yacl/base/buffer.h"
#include "yacl/link/context.h"

namespace pir::simple {
/// @brief Serializes a vector of 64-bit integers into network-ready binary
/// format
/// @param data Reference to source data vector containing uint64_t elements
/// @return yacl::Buffer with packed binary data
yacl::Buffer SerializeMessage(const std::vector<uint64_t> &data);

/// @brief Transmits data vector to peer node using asynchronous communication
/// @param data Vector of numerical values to send
/// @param lctx Network communication context handle
void SendData(const std::vector<uint64_t> &data,
              std::shared_ptr<yacl::link::Context> lctx);
}  // namespace pir::simple
