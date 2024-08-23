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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "psi/apsi_wrapper/utils/sender_db.h"

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
              bool compress);

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
  int32_t nonce_byte_count_ = 16;

  std::unordered_map<size_t, size_t> bucket_offset_map_;
  std::map<size_t, size_t> offset_bucket_map_;
};

class GroupDB {
 public:
  static inline const std::string KGroupDBVersion = "sf_pir_group_db_001";

  struct BucketIndex {
    size_t start_index;
    size_t cnt;
  };

  enum class DBState : int32_t {
    KNotExist = 0,
    KBucketed = 1,
    KGenerated = 2,
  };

  struct GroupDBStatus {
    std::string version = KGroupDBVersion;
    size_t group_cnt = 0;
    size_t num_buckets = 0;
    std::string params_file_content;
    DBState status = DBState::KNotExist;

    friend std::ostream& operator<<(std::ostream& os,
                                    const GroupDBStatus& status) {
      os << status.params_file_content << "\n";
      os << static_cast<int>(status.status) << " ";
      os << status.num_buckets << " ";
      os << status.group_cnt << " ";
      os << status.version << " ";

      return os;
    }

    friend std::istream& operator>>(std::istream& is, GroupDBStatus& status) {
      std::getline(is, status.params_file_content);
      int32_t tmp;
      is >> tmp;
      status.status = static_cast<DBState>(tmp);
      is >> status.num_buckets;
      is >> status.group_cnt;
      is >> status.version;

      return is;
    }
  };

  GroupDB(const std::string& source_file, const std::string& db_path,
          std::size_t group_cnt, size_t num_buckets,
          const std::string& params_file = "", bool compress = false);

  void DivideGroup();

  size_t GetBucketNum() { return num_buckets_; }

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
  std::string status_file_path_;
  MultiplexDiskCache disk_cache_;
  std::shared_ptr<::apsi::PSIParams> params_;
  bool compress_;
  std::unordered_map<size_t, std::shared_ptr<GroupDBItem>> group_map_;
  GroupDBStatus status_;
};

}  // namespace psi::apsi_wrapper
