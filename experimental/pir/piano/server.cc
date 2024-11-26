#include "experimental/pir/piano/server.h"

namespace pir::piano {

QueryServiceServer::QueryServiceServer(
    std::vector<uint8_t>& db, std::shared_ptr<yacl::link::Context> context,
    const uint64_t set_size, const uint64_t chunk_size,
    const uint64_t entry_size)
    : db_(std::move(db)),
      context_(std::move(context)),
      set_size_(set_size),
      chunk_size_(chunk_size),
      entry_size_(entry_size) {}

void QueryServiceServer::Start(const std::future<void>& stop_signal) {
  while (stop_signal.wait_for(std::chrono::milliseconds(1)) ==
         std::future_status::timeout) {
    auto request_data = context_->Recv(context_->NextRank(), "request_data");
    HandleRequest(request_data);
  }
}

void QueryServiceServer::HandleRequest(const yacl::Buffer& request_data) {
  QueryRequest proto;
  proto.ParseFromArray(request_data.data(), request_data.size());

  switch (proto.request_case()) {
    case QueryRequest::kFetchFullDb: {
      // uint64_t dummy = DeserializeFetchFullDBMsg(request_data);
      ProcessFetchFullDB();
      break;
    }
    case QueryRequest::kSetParityQuery: {
      const auto parityQuery = DeserializeSetParityQueryMsg(request_data);
      const auto& indices = std::get<1>(parityQuery);

      auto [parity, server_compute_time] = ProcessSetParityQuery(indices);
      const auto response_buf =
          SerializeSetParityQueryResponse(parity, server_compute_time);
      context_->SendAsync(context_->NextRank(), response_buf,
                          "SetParityQueryResponse");
      break;
    }
    default:
      SPDLOG_ERROR("Unknown request type.");
  }
}

void QueryServiceServer::ProcessFetchFullDB() {
  for (uint64_t i = 0; i < set_size_; ++i) {
    const uint64_t down = i * chunk_size_;
    uint64_t up = (i + 1) * chunk_size_;
    up = std::min(up, db_.size() / entry_size_);
    std::vector<uint8_t> chunk(db_.begin() + down * entry_size_,
                               db_.begin() + up * entry_size_);
    auto chunk_buf = SerializeDBChunk(i, chunk.size(), chunk);

    try {
      context_->SendAsync(context_->NextRank(), chunk_buf, "FetchFullDBChunk");
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Failed to send a chunk.");
      return;
    }
  }
}

std::pair<std::vector<uint8_t>, uint64_t>
QueryServiceServer::ProcessSetParityQuery(
    const std::vector<uint64_t>& indices) {
  const auto start = std::chrono::high_resolution_clock::now();
  std::vector<uint8_t> parity = HandleSetParityQuery(indices);
  const auto end = std::chrono::high_resolution_clock::now();
  const auto duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  return {parity, duration};
}

std::vector<uint8_t> QueryServiceServer::HandleSetParityQuery(
    const std::vector<uint64_t>& indices) {
  DBEntry parity = DBEntry::ZeroEntry(entry_size_);
  for (const auto& index : indices) {
    DBEntry entry = DBAccess(index);
    parity.Xor(entry);
  }
  return parity.data();
}

DBEntry QueryServiceServer::DBAccess(const uint64_t id) {
  if (const size_t num_entries = db_.size() / entry_size_; id < num_entries) {
    std::vector<uint8_t> slice(entry_size_);
    std::copy(db_.begin() + id * entry_size_,
              db_.begin() + (id + 1) * entry_size_, slice.begin());
    return DBEntry::DBEntryFromSlice(slice);
  }
  SPDLOG_ERROR("DBAccess: id {} out of range", id);
  return DBEntry::ZeroEntry(entry_size_);
}

}  // namespace pir::piano
