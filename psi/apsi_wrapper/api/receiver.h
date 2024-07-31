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

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "apsi/psi_params.h"

#include "psi/apsi_wrapper/receiver.h"

namespace psi::apsi_wrapper::api {

class Receiver {
 public:
  Receiver() = default;

  void SetThreadCount(size_t threads);

  // Step 1. - option a
  void LoadParamsConfig(const std::string& params_file);

  // Step 1. - option b
  void LoadSenderParams(const std::string& params_response_str);

  // Step 2.
  void LoadItems(const std::string& query_file);

  // Step 2.
  void LoadItems(const std::vector<std::string>& queries);

  // Step 3.
  std::string RequestOPRF(size_t bucket_idx = 0);

  // Step 4.
  std::string RequestQuery(const std::string& oprf_response_str,
                           size_t bucket_idx = 0);

  // Step 5.
  size_t ProcessResult(const std::string& query_response_str,
                       const std::string& output_file,
                       bool append_to_outfile = false);

  // Below APIs are experimental at this moment.
  std::vector<std::pair<size_t, std::vector<std::string>>> BucketizeItems(
      const std::vector<std::string>& queries, size_t bucket_cnt);

  std::vector<std::pair<size_t, std::vector<std::string>>> BucketizeItems(
      const std::string& query_file, size_t bucket_cnt);

 private:
  std::unique_ptr<::apsi::PSIParams> params_;

  std::vector<std::string> orig_items_;

  std::vector<::apsi::Item> items_vec_;

  std::unique_ptr<psi::apsi_wrapper::Receiver> receiver_;

  std::unique_ptr<::apsi::oprf::OPRFReceiver> oprf_receiver_;

  std::vector<::apsi::HashedItem> oprf_items_;

  std::vector<::apsi::LabelKey> label_keys_;

  ::apsi::receiver::IndexTranslationTable itt_;
};

}  // namespace psi::apsi_wrapper::api
