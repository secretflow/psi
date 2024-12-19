#pragma once

#include <vector>

#include "experimental/pir/piano/util.h"
#include "yacl/base/buffer.h"

#include "experimental/pir/piano/piano.pb.h"

namespace pir::piano {

inline yacl::Buffer SerializeFetchFullDB(const uint64_t dummy) {
  FetchFullDbProto proto;
  proto.set_dummy(dummy);
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline uint64_t DeserializeFetchFullDB(const yacl::Buffer& buf) {
  FetchFullDbProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  return proto.dummy();
}

inline yacl::Buffer SerializeDBChunk(const std::vector<uint8_t>& chunk) {
  DbChunkProto proto;
  proto.set_chunks(chunk.data(), chunk.size());
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::vector<uint8_t> DeserializeDBChunk(const yacl::Buffer& buf) {
  DbChunkProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint8_t> chunk(proto.chunks().begin(), proto.chunks().end());
  return chunk;
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
