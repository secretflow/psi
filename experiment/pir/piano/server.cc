#include "experiment/pir/piano/server.h"

namespace pir::piano {

QueryServiceServer::QueryServiceServer(
    std::shared_ptr<yacl::link::Context> context, std::vector<uint8_t>& db,
    const uint64_t entry_num, const uint64_t entry_size)
    : context_(std::move(context)),
      db_(std::move(db)),
      entry_num_(entry_num),
      entry_size_(entry_size) {
  std::tie(chunk_size_, set_size_) = GenChunkParams(entry_num_);
  AlignDBToChunkBoundary();
}

void QueryServiceServer::AlignDBToChunkBoundary() {
  if (entry_num_ < chunk_size_ * set_size_) {
    const uint64_t padding_num = (chunk_size_ * set_size_) - entry_num_;
    const uint64_t seed = yacl::crypto::FastRandU64();

    db_.reserve(db_.size() + (padding_num * entry_size_));
    for (uint64_t i = 0; i < padding_num; ++i) {
      auto padding_entry = DBEntry::GenDBEntry(entry_size_, seed, i, FNVHash);
      db_.insert(db_.end(), padding_entry.GetData().begin(),
                 padding_entry.GetData().end());
    }
    entry_num_ += padding_num;
  }
}

void QueryServiceServer::HandleFetchFullDB() {
  DeserializeFetchFullDB(context_->Recv(context_->NextRank(), "FetchFullDB"));
  for (uint64_t i = 0; i < set_size_; ++i) {
    const uint64_t down = i * chunk_size_;
    const uint64_t up = (i + 1) * chunk_size_;
    std::vector<uint8_t> chunk(db_.begin() + down * entry_size_,
                               db_.begin() + up * entry_size_);

    try {
      context_->SendAsync(context_->NextRank(), SerializeDBChunk(chunk),
                          "DBChunk");
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Failed to send a chunk: {}", e.what());
      return;
    }
  }
}

void QueryServiceServer::HandleMultipleQueries(
    const std::future<void>& stop_signal) {
  while (stop_signal.wait_for(std::chrono::milliseconds(5)) ==
         std::future_status::timeout) {
    HandleQueryRequest();
  }
}

void QueryServiceServer::HandleQueryRequest() {
  const auto indices = DeserializeSetParityQuery(
      context_->Recv(context_->NextRank(), "SetParityQuery"));

  const std::vector<uint8_t> parity = ProcessSetParityQuery(indices);
  context_->SendAsync(context_->NextRank(), SerializeSetParityResponse(parity),
                      "SetParityResponse");
}

std::vector<uint8_t> QueryServiceServer::ProcessSetParityQuery(
    const std::vector<uint64_t>& indices) {
  DBEntry parity = DBEntry::ZeroEntry(entry_size_);
  for (const auto& index : indices) {
    DBEntry entry = DBAccess(index);
    parity.Xor(entry);
  }
  return parity.GetData();
}

DBEntry QueryServiceServer::DBAccess(const uint64_t idx) {
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
