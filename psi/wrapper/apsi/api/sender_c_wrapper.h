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

extern "C" {

#include <stddef.h>
#include <stdint.h>

#include "psi/wrapper/apsi/api/wrapper_common.h"

struct Sender;

Sender *BucketSenderMake(const char *source_file, const char *db_path,
                         uint32_t group_cnt, uint32_t num_buckets,
                         uint32_t nonce_byte_count, const char *params_file,
                         bool compress);

Sender *BucketSenderLoad(const char *db_path, size_t thread_count);

void BucketSenderFree(Sender **sender);

CString BucketSenderGenerateParams(Sender *sender);

CString BucketSenderRunOPRFSingle(Sender *sender, CString oprf_request_str);

CString BucketSenderRunQuerySingle(Sender *sender, CString query_str);

CStringArray BucketSenderRunOPRFBatch(Sender *sender,
                                      CStringArray oprf_request_str);

CStringArray BucketSenderRunQueryBatch(Sender *sender, CStringArray query_str);
}
