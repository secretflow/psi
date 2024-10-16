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

#include "psi/apsi_wrapper/api/receiver_c_wrapper.h"

#include <memory>
#include <numeric>
#include <utility>

#include "spdlog/spdlog.h"

#include "psi/apsi_wrapper/api/receiver.h"
#include "psi/apsi_wrapper/api/wrapper_util.h"

using ApiReceiver = psi::apsi_wrapper::api::Receiver;
using ApiQueryCtx =
    std::vector<psi::apsi_wrapper::api::Receiver::BucketContext>;

Receiver* BucketReceiverMake(size_t bucket_cnt, size_t thread_count) {
  auto receiver = std::make_unique<ApiReceiver>(bucket_cnt, thread_count);
  return reinterpret_cast<Receiver*>(receiver.release());
}

void BucketReceiverFree(Receiver** receiver) {
  if (receiver != nullptr || *receiver == nullptr) {
    return;
  }
  (void)std::unique_ptr<ApiReceiver>(reinterpret_cast<ApiReceiver*>(*receiver));
  *receiver = nullptr;
}

void BucketReceiverLoadParamFile(Receiver* receiver, const char* params_file) {
  reinterpret_cast<ApiReceiver*>(receiver)->LoadParamsConfig(params_file);
}
void BucketReceiverLoadParamsStr(Receiver* receiver,
                                 CString params_response_str) {
  reinterpret_cast<ApiReceiver*>(receiver)->LoadSenderParams(
      psi::apsi_wrapper::api::MakeString(params_response_str));
}

void BucketReceiverSetThreadCount(Receiver* receiver, size_t threads) {
  reinterpret_cast<ApiReceiver*>(receiver)->SetThreadCount(threads);
}

QueryCtx* BucketReceiverMakeQueryCtxFromFile(Receiver* receiver,
                                             const char* query_ctx_file) {
  auto ctx =
      reinterpret_cast<ApiReceiver*>(receiver)->BucketizeItems(query_ctx_file);
  auto ctx_ptr = std::make_unique<ApiQueryCtx>(std::move(ctx));
  return reinterpret_cast<QueryCtx*>(ctx_ptr.release());
}

QueryCtx* BucketReceiverMakeQueryCtxFromItems(Receiver* receiver,
                                              CStringArray queries) {
  auto ctx = reinterpret_cast<ApiReceiver*>(receiver)->BucketizeItems(
      psi::apsi_wrapper::api::MakeStringArray(queries));
  auto ctx_ptr = std::make_unique<ApiQueryCtx>(std::move(ctx));
  return reinterpret_cast<QueryCtx*>(ctx_ptr.release());
}

void BucketReceiverFreeQueryCtx(QueryCtx* ctx) {
  (void)std::unique_ptr<ApiQueryCtx>(reinterpret_cast<ApiQueryCtx*>(ctx));
}

CStringArray BucketReceiverRequestOPRF(Receiver* receiver, QueryCtx* ctx) {
  return psi::apsi_wrapper::api::MakeCStringArray(
      reinterpret_cast<ApiReceiver*>(receiver)->RequestOPRF(
          *reinterpret_cast<ApiQueryCtx*>(ctx)));
}
CStringArray BucketReceiverRequestQuery(Receiver* receiver, QueryCtx* ctx,
                                        CStringArray oprf_responses) {
  return psi::apsi_wrapper::api::MakeCStringArray(
      reinterpret_cast<ApiReceiver*>(receiver)->RequestQuery(
          *reinterpret_cast<ApiQueryCtx*>(ctx),
          psi::apsi_wrapper::api::MakeStringArray(oprf_responses)));
}

uint32_t BucketReceiverProcessResultToFile(Receiver* receiver, QueryCtx* ctx,
                                           CStringArray query_responses,
                                           const char* output_file_name) {
  auto result = reinterpret_cast<ApiReceiver*>(receiver)->ProcessResult(
      *reinterpret_cast<ApiQueryCtx*>(ctx),
      psi::apsi_wrapper::api::MakeStringArray(query_responses),
      output_file_name);
  return std::accumulate(result.begin(), result.end(), 0);
}

QueryResult* MakeQueryResult(
    const std::pair<std::vector<std::string>, std::vector<std::string>>&
        result) {
  const auto& keys = result.first;
  const auto& values = result.second;
  if (keys.size() != values.size() && !values.empty()) {
    SPDLOG_ERROR("result size not matched: keys {} values {}", keys.size(),
                 values.size());
    return nullptr;
  }

  auto* query_result =
      reinterpret_cast<QueryResult*>(malloc(sizeof(QueryResult)));
  if (query_result == nullptr) {
    SPDLOG_ERROR("malloc failed");
    return nullptr;
  }
  query_result->row_count = keys.size();
  query_result->labeled = !values.empty();

  query_result->keys = psi::apsi_wrapper::api::MakeCStringArray(keys);
  query_result->values = values.empty()
                             ? CStringArray{nullptr, 0}
                             : psi::apsi_wrapper::api::MakeCStringArray(values);

  return query_result;
}

void FreeQueryResult(QueryResult* query_result) {
  if (query_result == nullptr) {
    return;
  }

  FreeCStringArray(&query_result->keys);
  if (query_result->values.data != nullptr) {
    FreeCStringArray(&query_result->values);
  }

  free(query_result);
}

QueryResult* BucketReceiverProcessResult(Receiver* receiver, QueryCtx* ctx,
                                         CStringArray query_responses) {
  auto result = reinterpret_cast<ApiReceiver*>(receiver)->ProcessResult(
      *reinterpret_cast<ApiQueryCtx*>(ctx),
      psi::apsi_wrapper::api::MakeStringArray(query_responses));
  return MakeQueryResult(result);
}
