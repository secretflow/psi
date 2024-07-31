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
#include <string>

#include "apsi/sender_db.h"

namespace psi::apsi_wrapper {

std::string GenerateDbPath(const std::string& parent_path, std::size_t index);

class BucketSenderDbSwitcher {
 public:
  BucketSenderDbSwitcher(const std::string& parent_folder, size_t bucket_cnt,
                         size_t init_idx = 0);

  void SetBucketIdx(size_t idx, bool forced_to_reload = false);

  size_t bucket_idx() const { return current_bucket_idx_; }

  std::shared_ptr<::apsi::sender::SenderDB> GetSenderDB();

  ::apsi::oprf::OPRFKey GetOPRFKey();

 private:
  std::string parent_folder_;

  size_t bucket_cnt_;

  size_t current_bucket_idx_;

  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;

  ::apsi::oprf::OPRFKey oprf_key_;
};

}  // namespace psi::apsi_wrapper
