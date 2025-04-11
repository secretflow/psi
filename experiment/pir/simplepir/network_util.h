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
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

namespace pir::simple {
/// @brief Serializes a vector of 64-bit integers into network-ready binary
/// format
/// @param data Reference to source data vector containing uint64_t elements
/// @return yacl::Buffer with packed binary data
yacl::Buffer SerializeVector(const std::vector<uint64_t> &data);

/// @brief Serializes a 128-bit integer into network-ready binary format
/// @param value Reference to the 128-bit integer to serialize
/// @return yacl::Buffer with packed binary data
yacl::Buffer SerializeUint128(uint128_t value);

/// @brief Deserializes binary buffer into vector of 64-bit unsigned integers
/// @param buffer Reference to binary data buffer containing serialized message
/// @return Vector of deserialized numerical values
std::vector<uint64_t> DeserializeVector(const yacl::Buffer &buffer);

/// @brief Deserializes a 128-bit integer from network-ready binary format
/// @param buffer Reference to binary data buffer containing serialized integer
/// @return Deserialized 128-bit integer
uint128_t DeserializeUint128(const yacl::Buffer &buffer);

/// @brief Transmits data vector to peer node using asynchronous communication
/// @param data Vector of numerical values to send
/// @param lctx Network communication context handle
void SendVector(const std::vector<uint64_t> &data,
                std::shared_ptr<yacl::link::Context> lctx);

/// @brief Transmits a 128-bit integer to peer node using asynchronous
/// communication
/// @param seed Reference to the 128-bit integer to send
/// @param lctx Network communication context handle
void SendUint128(uint128_t seed, std::shared_ptr<yacl::link::Context> lctx);

/// @brief Network reception handler with integrated deserialization
/// @param data Reference to the vector to receive and deserialize into
/// @param lctx Shared pointer to communication context managing network links
void RecvVector(std::vector<uint64_t> &data,
                std::shared_ptr<yacl::link::Context> lctx);

/// @brief Network reception handler for 128-bit integer with integrated
/// deserialization
/// @param seed Reference to the 128-bit integer to receive
/// @param lctx Shared pointer to communication context managing network links
void RecvUint128(uint128_t &seed, std::shared_ptr<yacl::link::Context> lctx);
}  // namespace pir::simple
