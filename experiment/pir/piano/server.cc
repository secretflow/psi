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

#include "experiment/pir/piano/server.h"

namespace pir::piano {

QueryServiceServer::QueryServiceServer(std::vector<uint8_t>& db,
                                       uint64_t entry_num, uint64_t entry_size)
    : db_(std::move(db)), entry_num_(entry_num), entry_size_(entry_size) {
  std::tie(chunk_size_, set_size_) = GenChunkParams(entry_num_);
  AlignDBToChunkBoundary();
}

void QueryServiceServer::AlignDBToChunkBoundary() {
  if (entry_num_ < chunk_size_ * set_size_) {
    uint64_t padding_num = (chunk_size_ * set_size_) - entry_num_;
    uint64_t seed = yacl::crypto::FastRandU64();

    db_.reserve(db_.size() + (padding_num * entry_size_));
    for (uint64_t i = 0; i < padding_num; ++i) {
      auto padding_entry = DBEntry::GenDBEntry(entry_size_, seed, i, FNVHash);
      db_.insert(db_.end(), padding_entry.GetData().begin(),
                 padding_entry.GetData().end());
    }
    entry_num_ += padding_num;
  }
}

yacl::Buffer QueryServiceServer::GetDBChunk(uint64_t chunk_index) {
  uint64_t down = chunk_index * chunk_size_;
  uint64_t up = (chunk_index + 1) * chunk_size_;
  std::vector<uint8_t> chunk(db_.begin() + down * entry_size_,
                             db_.begin() + up * entry_size_);
  yacl::Buffer chunk_buffer = SerializeDBChunk(chunk_index, chunk);
  return chunk_buffer;
}

yacl::Buffer QueryServiceServer::GenerateIndexReply(
    const yacl::Buffer& query_buffer) {
  const auto indices = DeserializeSetParityQuery(query_buffer);
  DBEntry parity = DBEntry::ZeroEntry(entry_size_);
  for (const auto& index : indices) {
    DBEntry entry = DBAccess(index);
    parity.Xor(entry);
  }
  return SerializeSetParityResponse(parity.GetData());
}

DBEntry QueryServiceServer::DBAccess(uint64_t idx) {
  if (idx < entry_num_) {
    std::vector<uint8_t> slice(entry_size_);
    std::copy(db_.begin() + idx * entry_size_,
              db_.begin() + (idx + 1) * entry_size_, slice.begin());
    return DBEntry::DBEntryFromSlice(slice);
  }
  SPDLOG_ERROR("DBAccess: idx {} out of range", idx);
  return DBEntry::ZeroEntry(entry_size_);
}

}  // namespace pir::piano
