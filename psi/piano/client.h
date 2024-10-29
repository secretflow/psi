#pragma once

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>

#include "yacl/crypto/rand/rand.h"
#include "yacl/link/context.h"

#include "psi/piano/serialize.h"
#include "psi/piano/util.h"

namespace psi::piano {

class LocalSet {
 public:
  uint32_t tag;  // the tag of the set
  DBEntry parity;
  uint64_t
      programmedPoint;  // identifier for the element replaced after refresh,
                        // differing from those expanded by PRFEval
  bool isProgrammed;

  LocalSet(const uint32_t tag, const DBEntry& parity,
           const uint64_t programmed_point, const bool is_programmed)
      : tag(tag),
        parity(parity),
        programmedPoint(programmed_point),
        isProgrammed(is_programmed) {}
};

class LocalBackupSet {
 public:
  uint32_t tag;
  DBEntry parityAfterPunct;

  LocalBackupSet(const uint32_t tag, const DBEntry& parity_after_punct)
      : tag(tag), parityAfterPunct(parity_after_punct) {}
};

class LocalBackupSetGroup {
 public:
  uint64_t consumed;
  std::vector<std::reference_wrapper<LocalBackupSet>> sets;

  LocalBackupSetGroup(
      const uint64_t consumed,
      const std::vector<std::reference_wrapper<LocalBackupSet>>& sets)
      : consumed(consumed), sets(sets) {}
};

class LocalReplacementGroup {
 public:
  uint64_t consumed;
  std::vector<uint64_t> indices;
  std::vector<DBEntry> value;

  LocalReplacementGroup(const uint64_t consumed,
                        const std::vector<uint64_t>& indices,
                        const std::vector<DBEntry>& value)
      : consumed(consumed), indices(indices), value(value) {}
};

class QueryServiceClient {
 public:
  static constexpr uint64_t FailureProbLog2 = 40;
  uint64_t totalQueryNum{};

  QueryServiceClient(uint64_t db_size, uint64_t thread_num,
                     std::shared_ptr<yacl::link::Context> context);

  void Initialize();
  void InitializeLocalSets();
  void FetchFullDB();
  void SendDummySet() const;
  DBEntry OnlineSingleQuery(uint64_t x);
  std::vector<DBEntry> OnlineMultipleQueries(
      const std::vector<uint64_t>& queries);

 private:
  uint64_t db_size_;
  uint64_t thread_num_;
  std::shared_ptr<yacl::link::Context> context_;

  uint64_t chunk_size_{};
  uint64_t set_size_{};
  uint64_t primary_set_num_{};
  uint64_t backup_set_num_per_chunk_{};
  uint64_t total_backup_set_num_{};
  PrfKey master_key_{};
  yacl::crypto::AES_KEY long_key_{};

  std::vector<LocalSet> primary_sets_;
  std::vector<LocalBackupSet> local_backup_sets_;
  std::map<uint64_t, DBEntry> local_cache_;
  std::map<uint64_t, DBEntry> local_miss_elements_;
  std::vector<LocalBackupSetGroup> local_backup_set_groups_;
  std::vector<LocalReplacementGroup> local_replacement_groups_;
};

}  // namespace psi::piano
