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

#include "experiment/pir/simplepir/network_util.h"

#include <cstring>
#include <memory>
#include <vector>

namespace pir::simple {
yacl::Buffer SerializeVector(const std::vector<uint64_t> &data) {
  // Calculate total buffer size: size header + elements
  size_t msg_size = sizeof(size_t) + data.size() * sizeof(uint64_t);
  yacl::Buffer buffer(msg_size);

  std::byte *data_ptr = buffer.data<std::byte>();
  size_t data_size = data.size();

  // Write element count header
  std::memcpy(data_ptr, &data_size, sizeof(data_size));
  data_ptr += sizeof(data_size);

  // Serialize each element sequentially
  for (auto element : data) {
    std::memcpy(data_ptr, &element, sizeof(element));
    data_ptr += sizeof(element);
  }
  return buffer;
}

yacl::Buffer SerializeUint128(uint128_t value) {
  // Create a buffer of size 16 bytes (128 bits)
  yacl::Buffer buffer(sizeof(uint128_t));
  std::memcpy(buffer.data<uint128_t>(), &value, sizeof(uint128_t));
  return buffer;
}

std::vector<uint64_t> DeserializeVector(const yacl::Buffer &buffer) {
  // Get pointer to raw binary data (byte access)
  const std::byte *data_ptr = buffer.data<std::byte>();

  // Step 1: Read number of elements from buffer header
  size_t data_size;
  std::memcpy(&data_size, data_ptr, sizeof(data_size));
  data_ptr += sizeof(data_size);

  // Initialize result vector with pre-allocated size
  std::vector<uint64_t> data(data_size);

  // Step 2: Deserialize each 64-bit unsigned integer element
  for (size_t i = 0; i < data_size; i++) {
    uint64_t element;
    std::memcpy(&element, data_ptr, sizeof(element));
    data[i] = element;
    data_ptr += sizeof(element);
  }

  return data;
}

uint128_t DeserializeUint128(const yacl::Buffer &buffer) {
  // Get pointer to raw binary data (byte access)
  const std::byte *data_ptr = buffer.data<std::byte>();

  // Read number of elements from buffer header
  uint128_t value;
  std::memcpy(&value, data_ptr, sizeof(value));
  return value;
}

void SendVector(const std::vector<uint64_t> &data,
                std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeVector(data);
  lctx->SendAsync(lctx->NextRank(), msg, "MsgSendToReceiver");
}

void SendUint128(uint128_t seed, std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeUint128(seed);
  lctx->SendAsync(lctx->NextRank(), msg, "Uint128SendToReceiver");
}

void RecvVector(std::vector<uint64_t> &data,
                std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = lctx->Recv(lctx->NextRank(), "MsgRecvFromSender");
  data = DeserializeVector(msg);
}

void RecvUint128(uint128_t &seed, std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = lctx->Recv(lctx->NextRank(), "Uint128RecvFromSender");
  seed = DeserializeUint128(msg);
}
}  // namespace pir::simple
