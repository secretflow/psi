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
#include "psi/apsi_wrapper/utils/group_db.h"

namespace psi::apsi_wrapper::api {

class Sender {
 public:
  struct Option {
    std::string source_file;
    std::string db_path;
    size_t group_cnt = 1;
    size_t num_buckets = 1;
    uint32_t nonce_byte_count = 16;
    bool compress = true;
    std::string params_file;
  };

 public:
  Sender(std::string db_path,
         size_t thread_count = std::thread::hardware_concurrency())
      : group_db_(db_path), thread_count_(thread_count) {}
  Sender(Option option,
         size_t thread_count = std::thread::hardware_concurrency())
      : group_db_(option.source_file, option.db_path, option.group_cnt,
                  option.num_buckets, option.nonce_byte_count,
                  option.params_file, option.compress),
        thread_count_(thread_count) {}

  void SetThreadCount(size_t threads);

  // Save sender db as file.
  // Step 1
  bool GenerateSenderDb();

  // Provide params to receiver if necessary.
  std::string GenerateParams();

  // Step 2.
  std::string RunOPRF(const std::string &oprf_request_str);
  std::vector<std::string> RunOPRF(
      const std::vector<std::string> &oprf_request_str);

  // Step 3.
  std::string RunQuery(const std::string &query_str);
  std::vector<std::string> RunQuery(const std::vector<std::string> &query_str);

 private:
  GroupDBItem::BucketDBItem GetDefaultDB();

  GroupDB group_db_;
  size_t thread_count_ = 1;
};

}  // namespace psi::apsi_wrapper::api
