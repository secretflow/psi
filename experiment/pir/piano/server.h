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

#include <spdlog/spdlog.h>

#include <cstdint>
#include <vector>

#include "experiment/pir/piano/serialize.h"
#include "experiment/pir/piano/util.h"

namespace pir::piano {

class QueryServiceServer {
 public:
  QueryServiceServer(std::vector<uint8_t>& db, uint64_t entry_num,
                     uint64_t entry_size);

  /**
   * @brief Handles server-side full database retrieval request.
   *
   * Transfers database in chunks using a pipelined approach, sending one chunk
   * at a time to minimize client-side storage requirements.
   */
  yacl::Buffer GetDBChunk(uint64_t chunk_index);

  // Process a set parity query by computing the XOR of all elements in the
  // query set
  yacl::Buffer GenerateIndexReply(const yacl::Buffer& query_buffer);

 private:
  /**
   * @brief Align the database to ensure uniformity and independence of query
   * distribution.
   *
   * Pads the database with additional entries to complete the last chunk,
   * guaranteeing consistent query distribution across all chunks.
   */
  void AlignDBToChunkBoundary();

  // Access the database and return the entry corresponding to the index
  DBEntry DBAccess(uint64_t idx);

  std::vector<uint8_t> db_;  // The database
  uint64_t set_size_{};      // The size of the set
  uint64_t chunk_size_{};    // The size of each chunk
  uint64_t entry_num_{};     // The number of database entry
  uint64_t entry_size_{};    // The size of database entry
};

}  // namespace pir::piano
