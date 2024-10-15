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

#include <cstdint>
extern "C" {
#include <stddef.h>
#include <stdint.h>

#include "psi/apsi_wrapper/api/wrapper_common.h"

struct Receiver;
struct QueryCtx;

struct QueryResult {
  CStringArray keys;
  CStringArray values;
  uint32_t row_count;
  bool labeled;
};

Receiver* BucketReceiverMake(size_t bucket_cnt, size_t thread_count);
void BucketReceiverFree(Receiver** receiver);

void BucketReceiverLoadParamFile(Receiver* receiver, const char* params_file);
void BucketReceiverLoadParamsStr(Receiver* receiver,
                                 CString params_response_str);

void BucketReceiverSetThreadCount(Receiver* receiver, size_t threads);

QueryCtx* BucketReceiverMakeQueryCtxFromFile(Receiver* receiver,
                                             const char* query_ctx_file);
QueryCtx* BucketReceiverMakeQueryCtxFromItems(Receiver* receiver,
                                              CStringArray queries);
void BucketReceiverFreeQueryCtx(QueryCtx* ctx);

CStringArray BucketReceiverRequestOPRF(Receiver* receiver, QueryCtx* ctx);

CStringArray BucketReceiverRequestQuery(Receiver* receiver, QueryCtx* ctx,
                                        CStringArray oprf_responses);

uint32_t BucketReceiverProcessResultToFile(Receiver* receiver, QueryCtx* ctx,
                                           CStringArray query_responses,
                                           const char* output_file_name);

QueryResult* BucketReceiverProcessResult(Receiver* receiver, QueryCtx* ctx,
                                         CStringArray query_responses);

void FreeQueryResult(QueryResult* query_result);
}
