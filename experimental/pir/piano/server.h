#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <future>
#include <memory>
#include <vector>

#include "experimental/pir/piano/serialize.h"
#include "experimental/pir/piano/util.h"
#include "yacl/link/context.h"

namespace pir::piano {

class QueryServiceServer {
 public:
  QueryServiceServer(std::shared_ptr<yacl::link::Context> context,
                     std::vector<uint8_t>& db, uint64_t entry_num,
                     uint64_t entry_size);

  /**
   * @brief Align the database to ensure uniformity and independence of query
   * distribution.
   *
   * Pads the database with additional entries to complete the last chunk,
   * guaranteeing consistent query distribution across all chunks.
   */
  void AlignDBToChunkBoundary();

  /**
   * @brief Handles server-side full database retrieval request.
   *
   * Transfers database in chunks using a pipelined approach, sending one chunk
   * at a time to minimize client-side storage requirements.
   */
  void HandleFetchFullDB();
  void HandleQueryRequest();
  void HandleMultipleQueries(const std::future<void>& stop_signal);

 private:
  // Access the database and return the entry corresponding to the index
  DBEntry DBAccess(uint64_t idx);

  // Process a set parity query by computing the XOR of all elements in the
  // query set
  std::vector<uint8_t> ProcessSetParityQuery(
      const std::vector<uint64_t>& indices);

  std::shared_ptr<yacl::link::Context> context_;  // The communication context
  std::vector<uint8_t> db_;                       // The database
  uint64_t set_size_{};                           // The size of the set
  uint64_t chunk_size_{};                         // The size of each chunk
  uint64_t entry_num_{};   // The number of database entry
  uint64_t entry_size_{};  // The size of database entry
};

}  // namespace pir::piano
