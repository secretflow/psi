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

#include "psi/wrapper/apsi/api/receiver.h"

#include <memory>
#include <utility>

#include "apsi/network/result_package.h"
#include "apsi/network/sender_operation.h"
#include "apsi/network/sender_operation_response.h"
#include "spdlog/spdlog.h"

#include "psi/wrapper/apsi/receiver.h"
#include "psi/wrapper/apsi/utils/common.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

using namespace std;

namespace psi::apsi_wrapper::api {

Receiver::Receiver(size_t bucket_cnt, size_t thread_count)
    : bucket_cnt_(bucket_cnt) {
  ::apsi::ThreadPoolMgr::SetThreadCount(thread_count);
}

void Receiver::SetThreadCount(size_t threads) {
  ::apsi::ThreadPoolMgr::SetThreadCount(threads);
}

void Receiver::LoadParamsConfig(const std::string& params_file) {
  params_ = psi::apsi_wrapper::BuildPsiParams(params_file);
}

void Receiver::LoadSenderParams(const std::string& params_response_str) {
  stringstream ss;
  ss << params_response_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
      make_unique<::apsi::network::SenderOperationResponseParms>();
  sop_response->load(ss);

  ::apsi::ParamsResponse response = ::apsi::to_params_response(sop_response);
  params_ = make_unique<::apsi::PSIParams>(*response->params);
}

std::vector<Receiver::BucketContext> Receiver::BucketizeItems(
    const std::vector<std::string>& queries) {
  std::unordered_map<size_t, std::vector<std::string>> bucket_item_map;

  for (const auto& query : queries) {
    int bucket_idx = std::hash<std::string>()(query) % bucket_cnt_;

    if (bucket_item_map.find(bucket_idx) == bucket_item_map.end()) {
      bucket_item_map[bucket_idx] = std::vector<std::string>();
    }

    bucket_item_map[bucket_idx].emplace_back(query);
  }

  std::vector<Receiver::BucketContext> contexts;
  contexts.reserve(bucket_item_map.size());

  for (const auto& it : bucket_item_map) {
    BucketContext ctx;
    ctx.bucket_idx = it.first;
    ctx.items = it.second;
    contexts.emplace_back(std::move(ctx));
  }

  return contexts;
}

std::vector<Receiver::BucketContext> Receiver::BucketizeItems(
    const std::string& query_file) {
  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(query_file);

  return BucketizeItems(orig_items);
}

std::vector<std::string> Receiver::RequestOPRF(
    std::vector<Receiver::BucketContext>& contexts) {
  std::vector<std::string> oprf_queries;
  for (auto& context : contexts) {
    context.item_vec =
        std::vector<::apsi::Item>{context.items.begin(), context.items.end()};
    context.oprf_receiver =
        std::make_shared<::apsi::oprf::OPRFReceiver>(context.item_vec);

    unique_ptr<::apsi::network::SenderOperation> sop =
        psi::apsi_wrapper::Receiver::CreateOPRFRequest(*context.oprf_receiver,
                                                       context.bucket_idx);
    ::apsi::network::SenderOperationHeader sop_header;
    sop_header.type = sop->type();
    stringstream ss;
    sop_header.save(ss);
    sop->save(ss);

    oprf_queries.push_back(ss.str());
  }

  return oprf_queries;
}

std::vector<std::string> Receiver::RequestQuery(
    std::vector<Receiver::BucketContext>& bucket_contexts,
    const std::vector<std::string>& oprf_responses) {
  YACL_ENFORCE(bucket_contexts.size() == oprf_responses.size(),
               "bucket_contexts {} and oprf_responses {} size not match",
               bucket_contexts.size(), oprf_responses.size());
  std::vector<std::string> res;
  for (size_t i = 0; i < bucket_contexts.size(); ++i) {
    auto& context = bucket_contexts[i];
    const auto& oprf_response = oprf_responses[i];
    stringstream ss;
    ss << oprf_response;
    ::apsi::network::SenderOperationHeader sop_header;
    sop_header.load(ss);

    unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
        make_unique<::apsi::network::SenderOperationResponseOPRF>();
    sop_response->load(ss);

    ::apsi::OPRFResponse response = ::apsi::to_oprf_response(sop_response);

    YACL_ENFORCE(context.oprf_receiver != nullptr,
                 "oprf_receiver is nullptr, {}", context.bucket_idx);

    auto [oprf_items, label_keys] = psi::apsi_wrapper::Receiver::ExtractHashes(
        response, *context.oprf_receiver);
    context.label_keys = std::move(label_keys);
    context.receiver = std::make_shared<psi::apsi_wrapper::Receiver>(*params_);

    auto query = context.receiver->create_query(oprf_items, context.bucket_idx);

    context.itt = std::move(query.second);

    stringstream ss_out;
    sop_header.type = query.first->type();
    sop_header.save(ss_out);
    query.first->save(ss_out);
    res.emplace_back(ss_out.str());
  }
  return res;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
Receiver::ProcessResult(std::vector<BucketContext>& bucket_contexts,
                        const std::vector<std::string>& query_responses) {
  YACL_ENFORCE(bucket_contexts.size() == query_responses.size(),
               "bucket_contexts {} and oprf_responses {} size not match",
               bucket_contexts.size(), query_responses.size());
  std::vector<std::string> keys;
  std::vector<std::string> values;
  for (size_t i = 0; i != bucket_contexts.size(); ++i) {
    auto& context = bucket_contexts[i];
    stringstream ss;
    ss << query_responses[i];
    ::apsi::network::SenderOperationHeader sop_header;
    sop_header.load(ss);

    unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
        make_unique<::apsi::network::SenderOperationResponseQuery>();
    sop_response->load(ss);

    ::apsi::QueryResponse response = ::apsi::to_query_response(sop_response);
    auto seal_context = context.receiver->get_seal_context();
    std::vector<::apsi::ResultPart> rps;
    while (rps.size() < response->package_count) {
      unique_ptr<::apsi::network::ResultPackage> rp(
          make_unique<::apsi::network::ResultPackage>());
      rp->load(ss, seal_context);
      rps.emplace_back(std::move(rp));
    }

    vector<::apsi::receiver::MatchRecord> query_result =
        context.receiver->process_result(context.label_keys, context.itt, rps);

    auto match = psi::apsi_wrapper::stream_intersection_results(
        context.items, context.item_vec, query_result, keys, values);
    SPDLOG_INFO("match: {}", match);
  }
  return {keys, values};
}

std::vector<size_t> Receiver::ProcessResult(
    std::vector<BucketContext>& bucket_contexts,
    const std::vector<std::string>& query_responses,
    const std::string& output_file) {
  YACL_ENFORCE(bucket_contexts.size() == query_responses.size(),
               "bucket_contexts {} and oprf_responses {} size not match",
               bucket_contexts.size(), query_responses.size());
  bool append_to_outfile = false;
  std::vector<size_t> result_size;
  for (size_t i = 0; i != bucket_contexts.size(); ++i) {
    auto& context = bucket_contexts[i];
    stringstream ss;
    ss << query_responses[i];
    ::apsi::network::SenderOperationHeader sop_header;
    sop_header.load(ss);

    unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
        make_unique<::apsi::network::SenderOperationResponseQuery>();
    sop_response->load(ss);

    ::apsi::QueryResponse response = ::apsi::to_query_response(sop_response);
    auto seal_context = context.receiver->get_seal_context();
    std::vector<::apsi::ResultPart> rps;
    while (rps.size() < response->package_count) {
      unique_ptr<::apsi::network::ResultPackage> rp(
          make_unique<::apsi::network::ResultPackage>());
      rp->load(ss, seal_context);
      rps.emplace_back(std::move(rp));
    }

    vector<::apsi::receiver::MatchRecord> query_result =
        context.receiver->process_result(context.label_keys, context.itt, rps);

    result_size.push_back(psi::apsi_wrapper::print_intersection_results(
        context.items, context.item_vec, query_result, output_file,
        append_to_outfile));
    append_to_outfile = true;
  }

  return result_size;
}

}  // namespace psi::apsi_wrapper::api
