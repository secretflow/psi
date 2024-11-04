#pragma once

#include <tuple>
#include <utility>
#include <vector>

#include "experimental/pir/piano/util.h"
#include "yacl/base/buffer.h"

#include "experimental/pir/piano/piano.pb.h"

namespace pir::piano {

inline yacl::Buffer SerializeFetchFullDBMsg(const uint64_t dummy) {
  QueryRequest proto;
  FetchFullDbMsg* fetch_full_db_msg = proto.mutable_fetch_full_db();
  fetch_full_db_msg->set_dummy(dummy);

  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());

  return buf;
}

inline uint64_t DeserializeFetchFullDBMsg(const yacl::Buffer& buf) {
  QueryRequest proto;
  proto.ParseFromArray(buf.data(), buf.size());
  return proto.fetch_full_db().dummy();
}

inline yacl::Buffer SerializeDBChunk(const uint64_t chunk_id,
                                     const uint64_t chunk_size,
                                     const std::vector<uint64_t>& chunk) {
  DbChunk proto;
  proto.set_chunk_id(chunk_id);
  proto.set_chunk_size(chunk_size);
  for (const auto& val : chunk) {
    proto.add_chunks(val);
  }
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::tuple<uint64_t, uint64_t, std::vector<uint64_t>> DeserializeDBChunk(
    const yacl::Buffer& buf) {
  DbChunk proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint64_t> chunk(proto.chunks().begin(), proto.chunks().end());
  return {proto.chunk_id(), proto.chunk_size(), chunk};
}

inline yacl::Buffer SerializeSetParityQueryMsg(
    const uint64_t set_size, const std::vector<uint64_t>& indices) {
  QueryRequest proto;
  SetParityQueryMsg* set_parity_query = proto.mutable_set_parity_query();
  set_parity_query->set_set_size(set_size);
  for (const auto& index : indices) {
    set_parity_query->add_indices(index);
  }

  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());

  return buf;
}

inline std::pair<uint64_t, std::vector<uint64_t>> DeserializeSetParityQueryMsg(
    const yacl::Buffer& buf) {
  QueryRequest proto;
  proto.ParseFromArray(buf.data(), buf.size());
  const auto& set_parity_query = proto.set_parity_query();
  std::vector<uint64_t> indices(set_parity_query.indices().begin(),
                                set_parity_query.indices().end());
  return {set_parity_query.set_size(), indices};
}

inline yacl::Buffer SerializeSetParityQueryResponse(
    const std::vector<uint64_t>& parity, const uint64_t server_compute_time) {
  SetParityQueryResponse proto;
  for (const auto& p : parity) {
    proto.add_parity(p);
  }
  proto.set_server_compute_time(server_compute_time);
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::pair<std::vector<uint64_t>, uint64_t>
DeserializeSetParityQueryResponse(const yacl::Buffer& buf) {
  SetParityQueryResponse proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint64_t> parity(proto.parity().begin(), proto.parity().end());
  return {parity, proto.server_compute_time()};
}

}  // namespace pir::piano
