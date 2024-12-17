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

#include "psi/wrapper/apsi/api/sender_c_wrapper.h"

#include <memory>

#include "sender.h"
#include "yacl/base/exception.h"

#include "psi/wrapper/apsi/api/sender.h"
#include "psi/wrapper/apsi/api/wrapper_util.h"

using ApiSender = psi::apsi_wrapper::api::Sender;

Sender *BucketSenderMake(const char *source_file, const char *db_path,
                         uint32_t group_cnt, uint32_t num_buckets,
                         uint32_t nonce_byte_count, const char *params_file,
                         bool compress) {
  ApiSender::Option option;
  option.source_file = source_file;
  option.db_path = db_path;
  option.group_cnt = group_cnt;
  option.num_buckets = num_buckets;
  option.nonce_byte_count = nonce_byte_count;
  option.compress = compress;
  option.params_file = params_file;
  auto sender = std::make_unique<ApiSender>(option);
  sender->GenerateSenderDb();
  return reinterpret_cast<Sender *>(sender.release());
}

Sender *BucketSenderLoad(const char *db_path, size_t thread_count) {
  auto sender = std::make_unique<ApiSender>(db_path, thread_count);
  return reinterpret_cast<Sender *>(sender.release());
}

void BucketSenderFree(Sender **sender) {
  YACL_ENFORCE(sender != nullptr, "sender is nullptr");
  (void)std::unique_ptr<ApiSender>(reinterpret_cast<ApiSender *>(*sender));
  *sender = nullptr;
}

CString BucketSenderGenerateParams(Sender *sender) {
  return psi::apsi_wrapper::api::MakeCString(
      reinterpret_cast<ApiSender *>(sender)->GenerateParams());
}

CString BucketSenderRunOPRFSingle(Sender *sender, CString oprf_request_str) {
  return psi::apsi_wrapper::api::MakeCString(
      reinterpret_cast<ApiSender *>(sender)->RunOPRF(
          psi::apsi_wrapper::api::MakeString(oprf_request_str)));
}

CString BucketSenderRunQuerySingle(Sender *sender, CString query_str) {
  return psi::apsi_wrapper::api::MakeCString(
      reinterpret_cast<ApiSender *>(sender)->RunQuery(
          psi::apsi_wrapper::api::MakeString(query_str)));
}

CStringArray BucketSenderRunOPRFBatch(Sender *sender,
                                      CStringArray oprf_request_str) {
  return psi::apsi_wrapper::api::MakeCStringArray(
      reinterpret_cast<ApiSender *>(sender)->RunOPRF(
          psi::apsi_wrapper::api::MakeStringArray(oprf_request_str)));
}

CStringArray BucketSenderRunQueryBatch(Sender *sender, CStringArray query_str) {
  return psi::apsi_wrapper::api::MakeCStringArray(
      reinterpret_cast<ApiSender *>(sender)->RunQuery(
          psi::apsi_wrapper::api::MakeStringArray(query_str)));
}
