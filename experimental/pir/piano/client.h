#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <utility>

#include "experimental/pir/piano/serialize.h"
#include "experimental/pir/piano/util.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/context.h"

namespace pir::piano {

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
  LocalSet(const uint32_t tag, DBEntry parity, const uint64_t programmed_point,
           const bool is_programmed)
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
  LocalBackupSet(const uint32_t tag, DBEntry parity_after_puncture)
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
  LocalBackupSetGroup(
      const uint64_t consumed,
      const std::vector<std::reference_wrapper<LocalBackupSet>>& sets)
      : consumed(consumed), sets(sets) {}

  uint64_t consumed;
  std::vector<std::reference_wrapper<LocalBackupSet>> sets;
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
  LocalReplacementGroup(const uint64_t consumed,
                        const std::vector<uint64_t>& indices,
                        const std::vector<DBEntry>& values)
      : consumed(consumed), indices(indices), values(values) {}

  uint64_t consumed;
  std::vector<uint64_t> indices;
  std::vector<DBEntry> values;
};

class QueryServiceClient {
 public:
  QueryServiceClient(std::shared_ptr<yacl::link::Context> context,
                     uint64_t entry_num, uint64_t thread_num,
                     uint64_t entry_size);

  void Initialize();
  void InitializeLocalSets();
  void FetchFullDB();
  void SendDummySet() const;
  DBEntry OnlineSingleQuery(uint64_t x);
  std::vector<DBEntry> OnlineMultipleQueries(
      const std::vector<uint64_t>& queries);
  uint64_t GetTotalQueryNumber() const { return total_query_num_; };

 private:
  // Statistical security parameter, representing log base 2
  const uint64_t log2_k_ = 40;
  // Converts log2_k_ from base-2 to natural logarithm using the change of base
  // formula
  const double natural_log_k_ = std::log(2) * static_cast<double>(log2_k_);

  std::shared_ptr<yacl::link::Context> context_;
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
