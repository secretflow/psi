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

#include "psi/apsi_wrapper/utils/bucket.h"

#include <filesystem>
#include <string>

#include "apsi/log.h"
#include "fmt/format.h"

#include "psi/apsi_wrapper/utils/sender_db.h"

namespace psi::apsi_wrapper {

std::string GenerateDbPath(const std::string& parent_path, std::size_t index) {
  std::filesystem::path path =
      std::filesystem::path(parent_path) / fmt::format("{}.db", index);

  return path.string();
}

BucketSenderDbSwitcher::BucketSenderDbSwitcher(const std::string& parent_folder,
                                               size_t bucket_cnt,
                                               size_t init_idx)
    : parent_folder_(parent_folder), bucket_cnt_(bucket_cnt) {
  SetBucketIdx(init_idx, true);
  (void)bucket_cnt_;
}

void BucketSenderDbSwitcher::SetBucketIdx(size_t idx, bool forced_to_reload) {
  if (!forced_to_reload && idx == current_bucket_idx_) {
    return;
  }

  std::string db_path = GenerateDbPath(parent_folder_, idx);

  sender_db_ = TryLoadSenderDB(db_path, "", oprf_key_);

  if (!sender_db_) {
    APSI_LOG_ERROR("Failed to create SenderDB in BucketSenderDbSwitcher.");
  }

  current_bucket_idx_ = idx;
}

std::shared_ptr<::apsi::sender::SenderDB>
BucketSenderDbSwitcher::GetSenderDB() {
  return sender_db_;
}

::apsi::oprf::OPRFKey BucketSenderDbSwitcher::GetOPRFKey() { return oprf_key_; }

}  // namespace psi::apsi_wrapper
