// Copyright 2024 The secretflow authors.
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

#include <vector>

#include "experiment/pir/piano/util.h"
#include "yacl/base/buffer.h"

#include "experiment/pir/piano/piano.pb.h"

namespace pir::piano {

inline yacl::Buffer SerializeDBChunk(uint64_t chunk_index,
                                     const std::vector<uint8_t>& chunk) {
  DbChunkProto proto;
  proto.set_chunk_index(chunk_index);
  proto.set_chunks(chunk.data(), chunk.size());
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::pair<uint64_t, std::vector<uint8_t>> DeserializeDBChunk(
    const yacl::Buffer& buf) {
  DbChunkProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint8_t> chunk(proto.chunks().begin(), proto.chunks().end());
  return {proto.chunk_index(), std::move(chunk)};
}

inline yacl::Buffer SerializeSetParityQuery(
    const std::vector<uint64_t>& indices) {
  SetParityQueryProto proto;
  for (const auto& index : indices) {
    proto.add_indices(index);
  }
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::vector<uint64_t> DeserializeSetParityQuery(
    const yacl::Buffer& buf) {
  SetParityQueryProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint64_t> indices(proto.indices().begin(), proto.indices().end());
  return indices;
}

inline yacl::Buffer SerializeSetParityResponse(
    const std::vector<uint8_t>& parity) {
  SetParityResponseProto proto;
  proto.set_parity(parity.data(), parity.size());
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::vector<uint8_t> DeserializeSetParityResponse(
    const yacl::Buffer& buf) {
  SetParityResponseProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint8_t> parity(proto.parity().begin(), proto.parity().end());
  return parity;
}

}  // namespace pir::piano
