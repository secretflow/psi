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

#include <apsi/oprf/oprf_receiver.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "apsi/psi_params.h"

#include "psi/wrapper/apsi/receiver.h"

namespace psi::apsi_wrapper::api {

class Receiver {
 public:
  Receiver(size_t bucket_cnt,
           size_t thread_count = std::thread::hardware_concurrency());

  void SetThreadCount(size_t threads);

  // Step 1. - option a
  void LoadParamsConfig(const std::string& params_file);

  // Step 1. - option b
  void LoadSenderParams(const std::string& params_response_str);

  struct BucketContext {
    size_t bucket_idx;
    std::vector<std::string> items;
    std::vector<::apsi::Item> item_vec;
    std::shared_ptr<::apsi::oprf::OPRFReceiver> oprf_receiver;
    std::string oprf_request;
    std::shared_ptr<psi::apsi_wrapper::Receiver> receiver;
    std::vector<::apsi::LabelKey> label_keys;
    ::apsi::receiver::IndexTranslationTable itt;
  };

  // Step 2.
  std::vector<Receiver::BucketContext> BucketizeItems(
      const std::vector<std::string>& queries);

  std::vector<Receiver::BucketContext> BucketizeItems(
      const std::string& query_file);

  // Step 3.
  std::vector<std::string> RequestOPRF(
      std::vector<Receiver::BucketContext>& contexts);

  // Step 4.
  std::vector<std::string> RequestQuery(
      std::vector<Receiver::BucketContext>& bucket_contexts,
      const std::vector<std::string>& oprf_responses);

  // Step 5.
  std::vector<size_t> ProcessResult(
      std::vector<BucketContext>& bucket_contexts,
      const std::vector<std::string>& query_responses,
      const std::string& output_file);

  std::pair<std::vector<std::string>, std::vector<std::string>> ProcessResult(
      std::vector<BucketContext>& bucket_contexts,
      const std::vector<std::string>& query_responses);

 private:
  size_t bucket_cnt_;
  std::unique_ptr<::apsi::PSIParams> params_;
};

}  // namespace psi::apsi_wrapper::api
