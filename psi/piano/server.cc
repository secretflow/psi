#include "psi/piano/server.h"

namespace psi::piano {

QueryServiceServer::QueryServiceServer(
    std::vector<uint64_t>& db, std::shared_ptr<yacl::link::Context> context,
    const uint64_t set_size, const uint64_t chunk_size)
    : db_(std::move(db)),
      context_(std::move(context)),
      set_size_(set_size),
      chunk_size_(chunk_size) {}

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
      auto [set_size, indices] = DeserializeSetParityQueryMsg(request_data);
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
    up = std::min(up, static_cast<uint64_t>(db_.size()));
    std::vector<uint64_t> chunk(db_.begin() + down * DBEntryLength,
                                db_.begin() + up * DBEntryLength);
    auto chunk_buf = SerializeDBChunk(i, chunk.size(), chunk);

    try {
      context_->SendAsync(context_->NextRank(), chunk_buf, "FetchFullDBChunk");
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Failed to send a chunk.");
      return;
    }
  }
}

std::pair<std::vector<uint64_t>, uint64_t>
QueryServiceServer::ProcessSetParityQuery(
    const std::vector<uint64_t>& indices) {
  const auto start = std::chrono::high_resolution_clock::now();
  std::vector<uint64_t> parity = HandleSetParityQuery(indices);
  const auto end = std::chrono::high_resolution_clock::now();
  const auto duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  return {parity, duration};
}

DBEntry QueryServiceServer::DBAccess(const uint64_t id) {
  if (id < db_.size()) {
    if (id * DBEntryLength + DBEntryLength > db_.size()) {
      SPDLOG_ERROR("DBAccess: id {} out of range", id);
    }
    std::array<uint64_t, DBEntryLength> slice{};
    std::copy(db_.begin() + id * DBEntryLength,
              db_.begin() + (id + 1) * DBEntryLength, slice.begin());
    return DBEntryFromSlice(slice);
  }
  DBEntry ret;
  ret.fill(0);
  return ret;
}

std::vector<uint64_t> QueryServiceServer::HandleSetParityQuery(
    const std::vector<uint64_t>& indices) {
  DBEntry parity = ZeroEntry();
  for (const auto& index : indices) {
    DBEntry entry = DBAccess(index);
    DBEntryXor(&parity, &entry);
  }

  std::vector<uint64_t> ret(DBEntryLength);
  std::copy(parity.begin(), parity.end(), ret.begin());
  return ret;
}

}  // namespace psi::piano
