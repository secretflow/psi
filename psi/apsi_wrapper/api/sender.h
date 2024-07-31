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

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include <cstddef>
#include <utility>

#include "apsi/query.h"
#include "apsi/requests.h"
#include "apsi/responses.h"

#include "psi/apsi_wrapper/utils/bucket.h"

namespace psi::apsi_wrapper::api {

class Sender {
 public:
  Sender() = default;

  void SetThreadCount(size_t threads);

  // Step 1. - option a
  bool LoadCsv(const std::string &csv_file_path,
               const std::string &params_file_path, size_t nonce_byte_count,
               bool compress);

  // Step 1. - option b
  bool LoadSenderDb(const std::string &sdb_file_path);

  // Save sender db as file.
  bool SaveSenderDb(const std::string &sdb_file_path);

  // Provide params to receiver if necessary.
  std::string GenerateParams();

  // Step 2.
  std::string RunOPRF(const std::string &oprf_request_str);

  // Step 3.
  std::string RunQuery(const std::string &query_str);

  // Below APIs are experimental at this moment.
  // Step 1.
  static bool SaveBucketizedSenderDb(const std::string &csv_file_path,
                                     const std::string &params_file_path,
                                     size_t nonce_byte_count, bool compress,
                                     const std::string &parent_path,
                                     size_t bucket_cnt);

  // Step 2.
  void LoadBucketizedSenderDb(const std::string &parent_path,
                              size_t bucket_cnt);

  // Step 3. & Step 4.
  // Reuse LoadSenderDb and SaveSenderDb.

 private:
  void LoadBucket();

  void SetBucketIdx(size_t idx);

  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;

  ::apsi::oprf::OPRFKey oprf_key_;

  std::shared_ptr<BucketSenderDbSwitcher> bucket_switcher_;
};

}  // namespace psi::apsi_wrapper::api
