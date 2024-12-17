// Copyright 2024 Ant Group Co., Ltd.
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

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "psi/wrapper/apsi/utils/sender_db.h"

#include "psi/wrapper/apsi/utils/group_db_status.pb.h"

namespace psi::apsi_wrapper {

class GroupDBItem {
 public:
  struct BucketDBItem {
    size_t bucket_id;
    std::shared_ptr<::apsi::sender::SenderDB> sender_db;
    ::apsi::oprf::OPRFKey oprf_key;
  };

  GroupDBItem(const std::string& source_file, const std::string& db_path,
              size_t group_idx, std::shared_ptr<::apsi::PSIParams> psi_params,
              uint32_t nonce_byte_count, bool compress, size_t max_bucket_cnt);

  GroupDBItem(const GroupDBItem&) = delete;
  GroupDBItem(GroupDBItem&&) = delete;
  GroupDBItem& operator=(const GroupDBItem&) = delete;
  GroupDBItem& operator=(GroupDBItem&&) = delete;

  void Generate();

  void LoadMeta();

  BucketDBItem LoadBucket(size_t bucket_id);

 private:
  std::string source_file_;
  std::string filename_;
  std::string meta_filename_;
  std::shared_ptr<::apsi::PSIParams> psi_params_;
  bool complete_ = false;
  bool compress_ = false;
  int32_t nonce_byte_count_;
  size_t max_bucket_cnt_ = 0;

  std::unordered_map<size_t, size_t> bucket_offset_map_;
  std::map<size_t, size_t> offset_bucket_map_;
};

class GroupDB {
 public:
  static inline const std::string KGroupDBVersion = "sf_pir_group_db.0.0.1";

  struct BucketIndex {
    size_t start_index;
    size_t cnt;
  };

  GroupDB(const std::string& source_file, const std::string& db_path,
          std::size_t group_cnt, size_t num_buckets,
          uint32_t nonce_byte_count = 16, const std::string& params_file = "",
          bool compress = false);

  explicit GroupDB(const std::string& db_path);

  void DivideGroup();

  size_t GetBucketNum() const { return num_buckets_; }

  std::string GetGroupSourceFile(size_t group_idx);

  size_t GetGroupNum();

  BucketIndex GetBucketIndexOfGroup(size_t group_idx);

  void GenerateGroup(size_t group_idx);

  size_t GetBucketGroupIdx(size_t bucket_idx);

  GroupDBItem::BucketDBItem GetBucketDB(size_t bucket_idx);

  bool IsDivided();

  bool IsDBGenerated();

  void GenerateDone();

  ~GroupDB();

 private:
  static inline const std::string status_file_name = "db.status";

  std::string source_file_;
  std::string db_path_;
  size_t group_cnt_;
  size_t num_buckets_;
  uint32_t nonce_byte_count_ = 16;
  std::string status_file_path_;
  MultiplexDiskCache disk_cache_;
  std::shared_ptr<::apsi::PSIParams> params_;
  bool compress_;
  std::unordered_map<size_t, std::shared_ptr<GroupDBItem>> group_map_;
  GroupDBStatus status_;
};

void GenerateGroupBucketDB(GroupDB& group_db, size_t process_num);

}  // namespace psi::apsi_wrapper
