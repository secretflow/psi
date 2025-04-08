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

#include "network_util.h"

#include <cstring>
#include <vector>

namespace pir::simple {
/// @brief Serializes a vector of 64-bit integers into a binary buffer
/// @param data Reference to the input vector containing uint64_t elements
/// @return yacl::Buffer containing serialized data
/// @remark Serialization format:
///         [size_t element_count] followed by [uint64_t elements...]
yacl::Buffer SerializeMessage(const std::vector<uint64_t> &data) {
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

// Deserialization function: Parses binary buffer into vector of uint64_t
// elements
// Data format: [data_size (size_t)] followed by [element1 (uint64_t), element2,
// ...]
// @param buffer: Binary buffer containing serialized data
// @return: Vector of deserialized uint64_t values
std::vector<uint64_t> DeserializeMessage(const yacl::Buffer &buffer) {
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

/// @brief Sends serialized data to network peer asynchronously
/// @param data Vector of uint64_t values to send
/// @param lctx Communication context for network operations
void SendData(const std::vector<uint64_t> &data,
              std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeMessage(data);
  lctx->SendAsync(lctx->NextRank(), msg, "MsgSendToReceiver");
}

// Generic data receiver: Handles message reception and deserialization
// @param lctx: Link context for network communication
// @return: Deserialized vector of uint64_t values from peer
std::vector<uint64_t> RecvData(std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = lctx->Recv(lctx->NextRank(), "MsgRecvFromSender");
  return DeserializeMessage(msg);
}
}  // namespace pir::simple
