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
  LocalSet(const uint32_t tag, DBEntry parity, const uint64_t programmed_point,
           const bool is_programmed)
      : tag(tag),
        parity(std::move(parity)),
        programmedPoint(programmed_point),
        isProgrammed(is_programmed) {}

  uint32_t tag;  // the tag of the set
  DBEntry parity;
  uint64_t programmedPoint;  // identifier for the element replaced after
                             // refresh differing from those expanded by PRFEval
  bool isProgrammed;
};

struct LocalBackupSet {
  LocalBackupSet(const uint32_t tag, DBEntry parity_after_puncture)
      : tag(tag), parityAfterPuncture(std::move(parity_after_puncture)) {}

  uint32_t tag;
  DBEntry parityAfterPuncture;
};

struct LocalBackupSetGroup {
  LocalBackupSetGroup(
      const uint64_t consumed,
      const std::vector<std::reference_wrapper<LocalBackupSet>>& sets)
      : consumed(consumed), sets(sets) {}

  uint64_t consumed;
  std::vector<std::reference_wrapper<LocalBackupSet>> sets;
};

struct LocalReplacementGroup {
  LocalReplacementGroup(const uint64_t consumed,
                        const std::vector<uint64_t>& indices,
                        const std::vector<DBEntry>& value)
      : consumed(consumed), indices(indices), value(value) {}

  uint64_t consumed;
  std::vector<uint64_t> indices;
  std::vector<DBEntry> value;
};

class QueryServiceClient {
 public:
  QueryServiceClient(uint64_t entry_num, uint64_t thread_num,
                     uint64_t entry_size,
                     std::shared_ptr<yacl::link::Context> context);

  void Initialize();
  void InitializeLocalSets();
  void FetchFullDB();
  void SendDummySet() const;
  DBEntry OnlineSingleQuery(uint64_t x);
  std::vector<DBEntry> OnlineMultipleQueries(
      const std::vector<uint64_t>& queries);
  uint64_t getTotalQueryNumber() const { return total_query_num_; };

 private:
  static constexpr uint64_t kFailureProbLog2 = 40;
  uint64_t total_query_num_{};
  uint64_t entry_num_;
  uint64_t thread_num_;
  std::shared_ptr<yacl::link::Context> context_;

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
