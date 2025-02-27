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
#include <utility>

#include "experiment/pir/piano/serialize.h"
#include "experiment/pir/piano/util.h"
#include "yacl/crypto/tools/prg.h"

namespace pir::piano {

// Statistical security parameter as log base 2
constexpr uint64_t kStatisticalSecurityLog2 = 40;

// Natural logarithm of the security parameter, ln(x) = log2(x) * ln(2)
constexpr double kStatisticalSecurityLn =
    std::log(2) * kStatisticalSecurityLog2;

struct LocalSet {
  /**
   * @brief Represents a compressed set in the primary table.
   *
   * @param tag Unique identifier for generating set elements via PRF. The j-th
   * offset is calculated as PRF(msk, tag||j).
   * @param parity XOR of all expanded set elements.
   * @param programmed_point Indicates an element requiring manual replacement.
   * Ensures balanced distribution by preserving query index in specific chunk.
   * @param is_programmed Signals whether manual modification is needed.
   */
  LocalSet(uint32_t tag, DBEntry parity, uint64_t programmed_point,
           bool is_programmed)
      : tag(tag),
        parity(std::move(parity)),
        programmed_point(programmed_point),
        is_programmed(is_programmed) {}

  uint32_t tag;
  DBEntry parity;
  // Identifier for the element replaced after refresh, differing from those
  // expanded by PRFEval
  uint64_t programmed_point;
  bool is_programmed;
};

struct LocalBackupSet {
  /**
   * @brief Represents a compressed set in the backup table.
   *
   * @param tag Functions similarly to the tag in the primary table, serving as
   * a unique identifier.
   * @param parity_after_puncture The XOR result of all elements in the set,
   * excluding an element in a specific chunk. This is designed to reduce the
   * computation needed when refreshing sets in the primary table.
   */
  LocalBackupSet(uint32_t tag, DBEntry parity_after_puncture)
      : tag(tag), parity_after_puncture(std::move(parity_after_puncture)) {}

  uint32_t tag;
  DBEntry parity_after_puncture;
};

struct LocalBackupSetGroup {
  /**
   * @brief Organizes backup sets into predefined chunks.
   *
   * @param consumed Indicates the current position of consumed sets within this
   * group.
   * @param sets Contains all backup sets related to a specific chunk, where
   * their parity_after_puncture values exclude elements from this chunk.
   */
  LocalBackupSetGroup(uint64_t consumed, absl::Span<LocalBackupSet> sets)
      : consumed(consumed), sets(sets) {}

  uint64_t consumed;
  absl::Span<LocalBackupSet> sets;
};

struct LocalReplacementGroup {
  /**
   * @brief Stores replacement entries for each chunk.
   *
   * @param consumed Indicates the current position of consumed replacement
   * entries within this chunk.
   * @param indices Randomly sampled indices generated from the current chunk.
   * @param values Values corresponding to the sampled indices.
   */
  LocalReplacementGroup(uint64_t consumed, const std::vector<uint64_t>& indices,
                        const std::vector<DBEntry>& values)
      : consumed(consumed), indices(indices), values(values) {}

  uint64_t consumed;
  std::vector<uint64_t> indices;
  std::vector<DBEntry> values;
};

struct QueryContext {
  bool is_mask_query = false;
  uint64_t current_query_index{};
  uint64_t hit_set_id{};
};

class QueryServiceClient {
 public:
  QueryServiceClient(uint64_t entry_num, uint64_t thread_num,
                     uint64_t entry_size);
  uint64_t GetTotalQueryNumber() const { return total_query_num_; };
  uint64_t GetChunkNumber() const { return set_size_; }

  /**
   * @brief Preprocess primary and backup set parities for a database chunk.
   *
   * This function processes a chunk of the database by:
   * 1. Deserializing the chunk buffer into the chunk index and entries.
   * 2. Using multiple threads to update primary and backup set parities based
   * on PRF-generated offsets, skipping backup sets belonging to the current
   * chunk.
   * 3. Identifying unmatched elements (local misses) and caching them.
   * 4. Sampling random replacement entries for the chunk's replacement groups.
   *
   * @param chunk_buffer The serialized buffer of the database chunk.
   */
  void PreprocessDBChunk(const yacl::Buffer& chunk_buffer);

  /**
   * @brief Generate a query request to fetch a database element by index.
   *
   * This function constructs a query for the specified `query_index`:
   * 1. If the index is in the local cache, it generates a mask query directly.
   * 2. Searches through primary sets to find a set that contains the index.
   * 3. If no match is found, it generates a mask query and caches the miss if
   * available.
   * 4. Expands the matched set and replaces the element corresponding to the
   * current chunk with a pre-sampled replacement value.
   * 5. Serializes and returns the edited set as a query message.
   *
   * @param query_index The index of the database element to query.
   * @return Serialized buffer containing the query message.
   */
  yacl::Buffer GenerateIndexQuery(uint64_t query_index);

  /**
   * @brief Recover the original database entry from the server's reply.
   *
   * Recover the original database entry by XORing the server's parity with:
   *  - The parity of the matched primary set.
   *  - The replacement value used in the query.
   *
   * Refreshe elements in the primary set using the backup set, updating the
   * parity accordingly to ensure privacy and load balancing.
   */
  DBEntry RecoverIndexReply(const yacl::Buffer& reply_buffer);

 private:
  /**
   * @brief Initialize query parameters and cryptographic keys.
   *
   * Computes the maximum number of queries supported, chunk sizes, and the
   * number of primary and backup sets, ensuring proper alignment with the
   * thread count.
   */
  void Initialize();

  // Initialize primary and backup sets along with their grouping structures
  void InitializeLocalSets();

  // Store results of sqrt(n) recent queries, serve duplicates locally while
  // masking with a random distinct query
  yacl::Buffer GenerateMaskQuery() const;

  QueryContext ctx_{};
  uint64_t total_query_num_{};
  uint64_t entry_num_{};
  uint64_t thread_num_{};

  uint64_t chunk_size_{};
  uint64_t set_size_{};
  uint64_t entry_size_{};
  uint64_t primary_set_num_{};
  uint64_t backup_set_num_per_chunk_{};
  uint64_t total_backup_set_num_{};
  uint128_t master_key_{};
  yacl::crypto::AES_KEY long_key_{};

  std::vector<LocalSet> primary_sets_;
  std::vector<LocalBackupSet> local_backup_sets_;
  std::unordered_map<uint64_t, DBEntry> local_cache_;
  std::unordered_map<uint64_t, DBEntry> local_miss_elements_;
  std::vector<LocalBackupSetGroup> local_backup_set_groups_;
  std::vector<LocalReplacementGroup> local_replacement_groups_;
};

}  // namespace pir::piano
