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

#include "psi/apsi_wrapper/utils/group_db.h"

#include <spdlog/spdlog.h>

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "arrow/array.h"
#include "fmt/format.h"
#include "sender_db.h"
#include "yacl/base/exception.h"

#include "psi/apsi_wrapper/utils/common.h"
#include "psi/apsi_wrapper/utils/csv_reader.h"

namespace psi::apsi_wrapper {

namespace {

constexpr const char* kGroupLabel = "value";
constexpr const char* kGroupKey = "key";
constexpr const char* kGroupBucketId = "bucket_id";

}  // namespace

GroupDBItem::GroupDBItem(const std::string& source_file,
                         const std::string& db_path, size_t group_idx,
                         std::shared_ptr<::apsi::PSIParams> psi_params,
                         bool compress, size_t max_bucket_cnt)
    : source_file_(source_file),
      filename_(fmt::format("{}/{}_group.db", db_path, group_idx)),
      meta_filename_(filename_ + ".meta"),
      psi_params_(psi_params),
      compress_(compress),
      max_bucket_cnt_(max_bucket_cnt) {}

void GroupDBItem::LoadMeta() {
  if (complete_) {
    return;
  }

  YACL_ENFORCE(std::filesystem::exists(filename_), "db file {} not exists.",
               filename_);
  YACL_ENFORCE(std::filesystem::exists(meta_filename_),
               "db file {} not exists.", meta_filename_);

  std::ifstream ifs = std::ifstream(filename_, std::ios::binary);
  std::ifstream mete_ifs = std::ifstream(meta_filename_);

  size_t bucket_num;
  mete_ifs >> bucket_num;

  YACL_ENFORCE_LE(bucket_num, max_bucket_cnt_,
                  "bucket_num {} is too large(more than {})", bucket_num,
                  max_bucket_cnt_);

  for (size_t i = 0; i < bucket_num; ++i) {
    size_t bucket_id;
    size_t offset;

    mete_ifs >> bucket_id >> offset;
    bucket_offset_map_[bucket_id] = offset;
    offset_bucket_map_[offset] = bucket_id;
  }

  complete_ = true;
}

GroupDBItem::BucketDBItem GroupDBItem::LoadBucket(size_t bucket_id) {
  if (bucket_offset_map_.empty()) {
    LoadMeta();
  }
  if (bucket_offset_map_.find(bucket_id) == bucket_offset_map_.end()) {
    return {0, nullptr, {}};
  }
  size_t offset = bucket_offset_map_[bucket_id];
  std::ifstream ifs = std::ifstream(filename_, std::ios::binary);
  ifs.seekg(offset);

  BucketDBItem bucket_db;
  bucket_db.bucket_id = bucket_id;
  bucket_db.sender_db = TryLoadSenderDB(ifs, bucket_db.oprf_key);

  return bucket_db;
}

bool IsGrouopLabeled(const std::string& source_file) {
  std::ifstream ifs(source_file);
  std::string line;
  YACL_ENFORCE(std::getline(ifs, line), "Failed to read file {}", source_file);
  return line.find(kGroupLabel) != std::string::npos;
}

void GroupDBItem::Generate() {
  if (complete_) {
    return;
  }

  if (std::filesystem::exists(filename_) &&
      std::filesystem::exists(meta_filename_)) {
    SPDLOG_INFO("DB file {} already exists, load_meta {} directly", filename_,
                meta_filename_);
    LoadMeta();
    return;
  }

  std::unordered_map<size_t, DBData> db_data;

  DBData result = UnlabeledData{};

  auto is_labeled = IsGrouopLabeled(source_file_);

  std::unordered_map<std::string, std::shared_ptr<arrow::DataType>> schema;
  schema[kGroupBucketId] = arrow::int64();
  schema[kGroupKey] = arrow::utf8();
  if (is_labeled) {
    schema[kGroupLabel] = arrow::utf8();
  }
  auto reader = MakeArrowCsvReader(source_file_, schema);

  std::shared_ptr<arrow::RecordBatch> batch;

  while (true) {
    auto status = reader->ReadNext(&batch);
    YACL_ENFORCE(status.ok(), "Read csv: {} error.", source_file_);

    if (batch == nullptr) {
      // Handle end of file
      break;
    }

    auto bucket_id_array =
        std::static_pointer_cast<arrow::Int64Array>(batch->column(0));
    auto key_array =
        std::static_pointer_cast<arrow::StringArray>(batch->column(1));
    std::shared_ptr<arrow::StringArray> label_array;
    if (is_labeled) {
      label_array =
          std::static_pointer_cast<arrow::StringArray>(batch->column(2));
    }

    auto row_cnt = batch->num_rows();
    for (int64_t i = 0; i < row_cnt; ++i) {
      auto bucket_id = bucket_id_array->Value(i);
      auto key = key_array->Value(i);
      auto value = is_labeled ? label_array->Value(i) : "";

      if (db_data.find(bucket_id) == db_data.end()) {
        if (is_labeled) {
          db_data[bucket_id] = LabeledData{};
        } else {
          db_data[bucket_id] = UnlabeledData{};
        }
      }

      if (is_labeled) {
        apsi::Label label(value.begin(), value.end());
        std::get<LabeledData>(db_data[bucket_id])
            .emplace_back(std::string(key), label);
      } else {
        std::get<UnlabeledData>(db_data[bucket_id])
            .emplace_back(std::string(key));
      }
    }
  }

  YACL_ENFORCE_LE(db_data.size(), max_bucket_cnt_,
                  "bucket_cnt {} is too large, more than {}", db_data.size(),
                  max_bucket_cnt_);

  std::vector<BucketDBItem> bucket_dbs_;

  std::ofstream ofs(filename_, std::ios::binary);
  ofs.exceptions(std::ios_base::badbit | std::ios_base::failbit);

  auto flush_proc = [&]() {
    size_t processed = 0;
    BucketDBItem* bucket_db;
    while (processed < db_data.size()) {
      {
        bucket_db = &bucket_dbs_[processed];
        ++processed;
      }
      bucket_offset_map_[bucket_db->bucket_id] = ofs.tellp();
      offset_bucket_map_[ofs.tellp()] = bucket_db->bucket_id;

      YACL_ENFORCE(
          TrySaveSenderDB(ofs, bucket_db->sender_db, bucket_db->oprf_key),
          "save sender db {} to {} failed.", bucket_db->bucket_id, filename_);
    }
  };

  for (auto& [bucket_id, data] : db_data) {
    BucketDBItem bucket_db;
    bucket_db.bucket_id = bucket_id;

    YACL_ENFORCE(!IsDuplicated(data),
                 "duplicated data in bucket {}, source_file: {}", bucket_id,
                 source_file_);

    if (is_labeled) {
      auto& labeled_db_data = std::get<LabeledData>(data);

      // Find the longest label and use that as label size
      size_t label_byte_count =
          max_element(labeled_db_data.begin(), labeled_db_data.end(),
                      [](auto& a, auto& b) {
                        return a.second.size() < b.second.size();
                      })
              ->second.size();

      bucket_db.sender_db = std::make_shared<::apsi::sender::SenderDB>(
          *psi_params_, label_byte_count, nonce_byte_count_, compress_);
      bucket_db.sender_db->set_data(labeled_db_data);
    } else {
      bucket_db.sender_db = std::make_shared<::apsi::sender::SenderDB>(
          *psi_params_, 0, 0, compress_);
      bucket_db.sender_db->set_data(std::get<UnlabeledData>(data));
    }
    bucket_db.oprf_key = bucket_db.sender_db->strip();

    { bucket_dbs_.push_back(bucket_db); }
  }

  // future.get();
  flush_proc();

  std::ofstream meta_ofs(meta_filename_);
  meta_ofs << bucket_offset_map_.size() << '\n';
  for (auto& [bucket_id, offset] : bucket_offset_map_) {
    meta_ofs << bucket_id << " " << offset << '\n';
  }

  complete_ = true;
}

void LoadStatus(const std::string& status_file,
                GroupDB::GroupDBStatus& status) {
  std::ifstream ifs(status_file);
  ifs >> status;
}

void SaveStatus(const std::string& status_file,
                const GroupDB::GroupDBStatus& status) {
  std::ofstream ofs(status_file);
  ofs << status;
}

GroupDB::GroupDB(const std::string& source_file, const std::string& db_path,
                 std::size_t group_cnt, size_t num_buckets,
                 const std::string& params_file, bool compress)
    : source_file_(source_file),
      db_path_(db_path),
      group_cnt_(group_cnt),
      num_buckets_(num_buckets),
      status_file_path_(std::filesystem::path(db_path_) / status_file_name),
      disk_cache_(db_path_, false, "group_"),
      params_(BuildPsiParams(params_file)),
      compress_(compress) {
  if (std::filesystem::exists(status_file_path_)) {
    LoadStatus(status_file_path_, status_);
    YACL_ENFORCE(status_.version == KGroupDBVersion,
                 "status version {}  not match {}, this dir may have a "
                 "different version of db, please choose a different dir",
                 status_.version, KGroupDBVersion);
    YACL_ENFORCE(status_.group_cnt == group_cnt_,
                 "group cnt {}  not match {}, this dir may have a "
                 "different version of db, please choose a different dir",
                 status_.group_cnt, group_cnt_);
    YACL_ENFORCE(status_.num_buckets == num_buckets_,
                 "bucket num {}  not match {}, this dir may have a "
                 "different version of db, please choose a different dir",
                 status_.num_buckets, num_buckets_);
    YACL_ENFORCE(status_.params_file_content == params_->to_string(),
                 "params {}  not match {}, this dir may have a "
                 "different version of db, please choose a different dir",
                 status_.params_file_content, params_->to_string());

  } else {
    status_ = GroupDBStatus{.version = KGroupDBVersion,
                            .group_cnt = group_cnt_,
                            .num_buckets = num_buckets_,
                            .params_file_content = params_->to_string(),
                            .status = DBState::KNotExist};
    SaveStatus(status_file_path_, status_);
  }
}

bool GroupDB::IsDivided() { return status_.status >= DBState::KBucketed; }

bool GroupDB::IsDBGenerated() { return status_.status >= DBState::KGenerated; }

void GroupDB::DivideGroup() {
  if (IsDivided()) {
    SPDLOG_INFO("It seems like the file has been divided, skip.");
    return;
  }

  if (!std::filesystem::exists(db_path_)) {
    SPDLOG_INFO("create bucket folder {}", db_path_);
    std::filesystem::create_directories(db_path_);
  }

  ApsiCsvReader reader(source_file_);
  reader.GroupBucketize(num_buckets_, db_path_, group_cnt_, disk_cache_);

  status_.status = DBState::KBucketed;
  SaveStatus(status_file_path_, status_);
}

size_t GroupDB::GetGroupNum() { return group_cnt_; }

GroupDB::BucketIndex GroupDB::GetBucketIndexOfGroup(size_t group_idx) {
  auto per_group_bucket_num = (num_buckets_ + group_cnt_ - 1) / group_cnt_;
  auto beg = group_idx * per_group_bucket_num;
  beg = std::min(num_buckets_, beg);
  auto end = std::min(num_buckets_, (group_idx + 1) * per_group_bucket_num);

  return BucketIndex{beg, end - beg};
}

void GroupDB::GenerateGroup(size_t group_idx) {
  auto per_group_bucket_num = (num_buckets_ + group_cnt_ - 1) / group_cnt_;

  auto group_item_db = std::make_shared<GroupDBItem>(
      disk_cache_.GetPath(group_idx), db_path_, group_idx, params_, compress_,
      per_group_bucket_num);
  group_item_db->Generate();
  group_map_[group_idx] = group_item_db;
}

void GroupDB::GenerateDone() {
  status_.status = DBState::KGenerated;
  SaveStatus(status_file_path_, status_);
}

size_t GroupDB::GetBucketGroupIdx(size_t bucket_idx) {
  YACL_ENFORCE(bucket_idx < num_buckets_,
               "bucket_idx {} is out of range: [0, {})", bucket_idx,
               num_buckets_);
  return bucket_idx / ((num_buckets_ + group_cnt_ - 1) / group_cnt_);
}

GroupDBItem::BucketDBItem GroupDB::GetBucketDB(size_t bucket_idx) {
  auto group_idx = GetBucketGroupIdx(bucket_idx);
  if (group_map_.find(group_idx) == group_map_.end()) {
    GenerateGroup(group_idx);
  }
  return group_map_[group_idx]->LoadBucket(bucket_idx);
}

GroupDB::~GroupDB() {}

}  // namespace psi::apsi_wrapper
