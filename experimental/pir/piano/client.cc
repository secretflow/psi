#include "experimental/pir/piano/client.h"

namespace pir::piano {

uint64_t primaryNumParam(const double q, const double chunk_size,
                         const double target) {
  const double k = std::ceil((std::log(2) * target) + std::log(q));
  return static_cast<uint64_t>(k) * static_cast<uint64_t>(chunk_size);
}

double FailProbBallIntoBins(const uint64_t ball_num, const uint64_t bin_num,
                            const uint64_t bin_size) {
  const double mean =
      static_cast<double>(ball_num) / static_cast<double>(bin_num);
  const double c = (static_cast<double>(bin_size) / mean) - 1;
  // Chernoff bound exp(-(c^2)/(2+c) * mean)
  double t = (mean * (c * c) / (2 + c)) * std::log(2);
  t -= std::log2(static_cast<double>(bin_num));
  return t;
}

QueryServiceClient::QueryServiceClient(
    const uint64_t db_size, const uint64_t thread_num,
    std::shared_ptr<yacl::link::Context> context)
    : db_size_(db_size), thread_num_(thread_num), context_(std::move(context)) {
  Initialize();
  InitializeLocalSets();
}

void QueryServiceClient::Initialize() {
  std::mt19937_64 rng(yacl::crypto::FastRandU64());

  master_key_ = RandKey(rng);
  long_key_ = GetLongKey(&master_key_);

  // Q = sqrt(n) * ln(n)
  totalQueryNum =
      static_cast<uint64_t>(std::sqrt(static_cast<double>(db_size_)) *
                            std::log(static_cast<double>(db_size_)));

  std::tie(chunk_size_, set_size_) = GenParams(db_size_);

  primary_set_num_ =
      primaryNumParam(static_cast<double>(totalQueryNum),
                      static_cast<double>(chunk_size_), FailureProbLog2 + 1);
  // if localSetNum is not a multiple of thread_num_ then we need to add some
  // padding
  primary_set_num_ =
      (primary_set_num_ + thread_num_ - 1) / thread_num_ * thread_num_;

  backup_set_num_per_chunk_ =
      3 * static_cast<uint64_t>(static_cast<double>(totalQueryNum) /
                                static_cast<double>(set_size_));
  backup_set_num_per_chunk_ =
      (backup_set_num_per_chunk_ + thread_num_ - 1) / thread_num_ * thread_num_;

  // set_size == chunk_number
  total_backup_set_num_ = backup_set_num_per_chunk_ * set_size_;
}

void QueryServiceClient::InitializeLocalSets() {
  primary_sets_.clear();
  primary_sets_.reserve(primary_set_num_);
  local_backup_sets_.clear();
  local_backup_sets_.reserve(total_backup_set_num_);
  local_cache_.clear();
  local_miss_elements_.clear();
  uint32_t tagCounter = 0;

  for (uint64_t j = 0; j < primary_set_num_; j++) {
    primary_sets_.emplace_back(tagCounter, ZeroEntry(), 0, false);
    tagCounter += 1;
  }

  local_backup_set_groups_.clear();
  local_backup_set_groups_.reserve(set_size_);
  local_replacement_groups_.clear();
  local_replacement_groups_.reserve(set_size_);

  for (uint64_t i = 0; i < set_size_; i++) {
    std::vector<std::reference_wrapper<LocalBackupSet>> backupSets;
    for (uint64_t j = 0; j < backup_set_num_per_chunk_; j++) {
      backupSets.emplace_back(
          local_backup_sets_[(i * backup_set_num_per_chunk_) + j]);
    }
    LocalBackupSetGroup backupGroup(0, backupSets);
    local_backup_set_groups_.emplace_back(std::move(backupGroup));

    std::vector<uint64_t> indices(backup_set_num_per_chunk_);
    std::vector<DBEntry> values(backup_set_num_per_chunk_);
    LocalReplacementGroup replacementGroup(0, indices, values);
    local_replacement_groups_.emplace_back(std::move(replacementGroup));
  }

  for (uint64_t j = 0; j < set_size_; j++) {
    for (uint64_t k = 0; k < backup_set_num_per_chunk_; k++) {
      local_backup_set_groups_[j].sets[k].get() =
          LocalBackupSet{tagCounter, ZeroEntry()};
      tagCounter += 1;
    }
  }
}

void QueryServiceClient::FetchFullDB() {
  const auto fetchFullDBMsg = SerializeFetchFullDBMsg(1);
  context_->SendAsync(context_->NextRank(), fetchFullDBMsg, "FetchFullDBMsg");

  for (uint64_t i = 0; i < set_size_; i++) {
    auto chunkBuf = context_->Recv(context_->NextRank(), "DBChunk");
    if (chunkBuf.size() == 0) {
      break;
    }
    auto dbChunk = DeserializeDBChunk(chunkBuf);
    auto& chunk = std::get<2>(dbChunk);

    std::vector<bool> hitMap(chunk_size_, false);

    // Use multiple threads to parallelize the computation for the chunk
    std::vector<std::thread> threads;
    std::mutex hitMapMutex;

    // make sure all sets are covered
    const uint64_t perTheadSetNum =
        ((primary_set_num_ + thread_num_ - 1) / thread_num_) + 1;
    const uint64_t perThreadBackupNum =
        ((total_backup_set_num_ + thread_num_ - 1) / thread_num_) + 1;

    for (uint64_t tid = 0; tid < thread_num_; tid++) {
      uint64_t startIndex = tid * perTheadSetNum;
      uint64_t endIndex =
          std::min(startIndex + perTheadSetNum, primary_set_num_);

      uint64_t startIndexBackup = tid * perThreadBackupNum;
      uint64_t endIndexBackup = std::min(startIndexBackup + perThreadBackupNum,
                                         total_backup_set_num_);

      threads.emplace_back([&, startIndex, endIndex, startIndexBackup,
                            endIndexBackup] {
        // update the parities for the primary hints
        for (uint64_t j = startIndex; j < endIndex; j++) {
          const auto tmp =
              PRFEvalWithLongKeyAndTag(long_key_, primary_sets_[j].tag, i);
          const auto offset = tmp & (chunk_size_ - 1);
          {
            std::lock_guard<std::mutex> lock(hitMapMutex);
            hitMap[offset] = true;
          }
          DBEntryXorFromRaw(&primary_sets_[j].parity,
                            &chunk[offset * DBEntryLength]);
        }

        // update the parities for the backup hints
        for (uint64_t j = startIndexBackup; j < endIndexBackup; j++) {
          const auto tmp =
              PRFEvalWithLongKeyAndTag(long_key_, local_backup_sets_[j].tag, i);
          const auto offset = tmp & (chunk_size_ - 1);
          DBEntryXorFromRaw(&local_backup_sets_[j].parityAfterPunct,
                            &chunk[offset * DBEntryLength]);
        }
      });
    }

    for (auto& thread : threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    // If any element is not hit, then it is a local miss. We will save it in
    // the local miss cache. Most of the time, the local miss cache will be
    // empty.
    for (uint64_t j = 0; j < chunk_size_; j++) {
      if (!hitMap[j]) {
        std::array<uint64_t, DBEntryLength> entry_slice{};
        std::memcpy(entry_slice.data(), &chunk[j * DBEntryLength],
                    DBEntryLength * sizeof(uint64_t));
        const auto entry = DBEntryFromSlice(entry_slice);
        local_miss_elements_[j + (i * chunk_size_)] = entry;
      }
    }

    // For the i-th group of backups, leave the i-th chunk as blank
    // To do that, we just xor the i-th chunk's value again
    for (uint64_t k = 0; k < backup_set_num_per_chunk_; k++) {
      const auto tag = local_backup_set_groups_[i].sets[k].get().tag;
      const auto tmp = PRFEvalWithLongKeyAndTag(long_key_, tag, i);
      const auto offset = tmp & (chunk_size_ - 1);
      DBEntryXorFromRaw(
          &local_backup_set_groups_[i].sets[k].get().parityAfterPunct,
          &chunk[offset * DBEntryLength]);
    }

    // store the replacement
    std::mt19937_64 rng(yacl::crypto::FastRandU64());
    for (uint64_t k = 0; k < backup_set_num_per_chunk_; k++) {
      // generate a random offset between 0 and ChunkSize - 1
      const auto offset = rng() & (chunk_size_ - 1);
      local_replacement_groups_[i].indices[k] = offset + i * chunk_size_;
      std::array<uint64_t, DBEntryLength> entry_slice{};
      std::memcpy(entry_slice.data(), &chunk[offset * DBEntryLength],
                  DBEntryLength * sizeof(uint64_t));
      local_replacement_groups_[i].value[k] = DBEntryFromSlice(entry_slice);
    }
  }
}

void QueryServiceClient::SendDummySet() const {
  std::mt19937_64 rng(yacl::crypto::FastRandU64());
  std::vector<uint64_t> randSet(set_size_);
  for (uint64_t i = 0; i < set_size_; i++) {
    randSet[i] = rng() % chunk_size_ + i * chunk_size_;
  }

  // send the random dummy set to the server
  const auto query_msg = SerializeSetParityQueryMsg(set_size_, randSet);
  context_->SendAsync(context_->NextRank(), query_msg, "SetParityQueryMsg");

  const auto response_buf =
      context_->Recv(context_->NextRank(), "SetParityQueryResponse");
  // auto parityQueryResponse = DeserializeSetParityQueryResponse(response_buf);
}

DBEntry QueryServiceClient::OnlineSingleQuery(const uint64_t x) {
  // make sure x is not in the local cache
  if (local_cache_.find(x) != local_cache_.end()) {
    SendDummySet();
    return local_cache_[x];
  }

  // 1. Query x: the client first finds a local set that contains x
  // 2. The client expands the set, replace the chunk(x)-th element to a
  // replacement
  // 3. The client sends the edited set to the server and gets the parity
  // 4. The client recovers the answer
  uint64_t hitSetId = std::numeric_limits<uint64_t>::max();

  const uint64_t queryOffset = x % chunk_size_;
  const uint64_t chunkId = x / chunk_size_;

  for (uint64_t i = 0; i < primary_set_num_; i++) {
    const auto& set = primary_sets_[i];
    if (const bool isProgrammedMatch =
            set.isProgrammed && chunkId == (set.programmedPoint / chunk_size_);
        !isProgrammedMatch &&
        PRSetWithShortTag{set.tag}.MemberTestWithLongKeyAndTag(
            long_key_, chunkId, queryOffset, chunk_size_)) {
      hitSetId = i;
      break;
    }
  }

  DBEntry xVal = ZeroEntry();

  if (hitSetId == std::numeric_limits<uint64_t>::max()) {
    if (local_miss_elements_.find(x) == local_miss_elements_.end()) {
      SPDLOG_ERROR("No hit set found for %lu", x);
    } else {
      xVal = local_miss_elements_[x];
      local_cache_[x] = xVal;
    }

    SendDummySet();
    return xVal;
  }

  // expand the set
  const PRSetWithShortTag set{primary_sets_[hitSetId].tag};
  auto expandedSet = set.ExpandWithLongKey(long_key_, set_size_, chunk_size_);

  // manually program the set if the flag is set before
  if (primary_sets_[hitSetId].isProgrammed) {
    const uint64_t programmedChunkId =
        primary_sets_[hitSetId].programmedPoint / chunk_size_;
    expandedSet[programmedChunkId] = primary_sets_[hitSetId].programmedPoint;
  }

  // edit the set by replacing the chunk(x)-th element with a replacement
  const uint64_t nxtAvailable = local_replacement_groups_[chunkId].consumed;
  if (nxtAvailable == backup_set_num_per_chunk_) {
    SPDLOG_ERROR("No replacement available for %lu", x);
    SendDummySet();
    return xVal;
  }

  // consume one replacement
  const uint64_t repIndex =
      local_replacement_groups_[chunkId].indices[nxtAvailable];
  const DBEntry repVal = local_replacement_groups_[chunkId].value[nxtAvailable];
  local_replacement_groups_[chunkId].consumed++;
  expandedSet[chunkId] = repIndex;

  // send the edited set to the server
  const auto query_msg = SerializeSetParityQueryMsg(set_size_, expandedSet);
  context_->SendAsync(context_->NextRank(), query_msg, "SetParityQueryMsg");

  const auto response_buf =
      context_->Recv(context_->NextRank(), "SetParityQueryResponse");

  const auto parityQueryResponse =
      DeserializeSetParityQueryResponse(response_buf);
  const auto& parity = std::get<0>(parityQueryResponse);

  // recover the answer
  xVal = primary_sets_[hitSetId].parity;    // the parity of the hit set
  DBEntryXorFromRaw(&xVal, parity.data());  // xor the parity of the edited set
  DBEntryXor(&xVal, &repVal);               // xor the replacement value

  // update the local cache
  local_cache_[x] = xVal;

  // refresh phase
  if (local_backup_set_groups_[chunkId].consumed == backup_set_num_per_chunk_) {
    SPDLOG_WARN("No backup set available for %lu", x);
    return xVal;
  }

  const DBEntry originalXVal = xVal;
  const uint64_t consumed = local_backup_set_groups_[chunkId].consumed;
  primary_sets_[hitSetId].tag =
      local_backup_set_groups_[chunkId].sets[consumed].get().tag;
  // backup set doesn't XOR the chunk(x)-th element in preparation
  DBEntryXor(
      &xVal,
      &local_backup_set_groups_[chunkId].sets[consumed].get().parityAfterPunct);
  primary_sets_[hitSetId].parity = xVal;
  primary_sets_[hitSetId].isProgrammed = true;
  // for load balancing, the chunk(x)-th element differs from the one expanded
  // via PRFEval on the tag
  primary_sets_[hitSetId].programmedPoint = x;
  local_backup_set_groups_[chunkId].consumed++;

  return originalXVal;
}

std::vector<DBEntry> QueryServiceClient::OnlineMultipleQueries(
    const std::vector<uint64_t>& queries) {
  std::vector<DBEntry> results;
  results.reserve(queries.size());

  for (const auto& x : queries) {
    DBEntry result = OnlineSingleQuery(x);
    results.push_back(result);
  }

  return results;
}

}  // namespace pir::piano
