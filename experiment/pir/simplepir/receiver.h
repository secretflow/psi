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
/// @brief Deserializes binary buffer into vector of 64-bit unsigned integers
/// @param buffer Reference to binary data buffer containing serialized message
/// @return Vector of deserialized numerical values
std::vector<uint64_t> DeserializeMessage(const yacl::Buffer &buffer);

/// @brief Network reception handler with integrated deserialization
/// @param lctx Shared pointer to communication context managing network links
/// @return Deserialized data vector received from peer
std::vector<uint64_t> RecvData(std::shared_ptr<yacl::link::Context> lctx);
}  // namespace pir::simple
