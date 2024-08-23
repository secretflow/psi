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

#include "psi/apsi_wrapper/api/receiver.h"

#include <memory>
#include <utility>

#include "apsi/network/result_package.h"
#include "apsi/network/sender_operation.h"
#include "apsi/network/sender_operation_response.h"

#include "psi/apsi_wrapper/receiver.h"
#include "psi/apsi_wrapper/utils/common.h"
#include "psi/apsi_wrapper/utils/sender_db.h"

using namespace std;

namespace psi::apsi_wrapper::api {

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

void Receiver::LoadItems(const std::string& query_file) {
  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(query_file);

  auto& items = get<psi::apsi_wrapper::UnlabeledData>(*query_data);
  items_vec_ = vector<::apsi::Item>(items.begin(), items.end());
  orig_items_ = std::move(orig_items);
}

void Receiver::LoadItems(const std::vector<std::string>& queries) {
  items_vec_.clear();

  for (const auto& query : queries) {
    items_vec_.emplace_back(query);
  }

  orig_items_ = queries;
}

std::vector<std::pair<size_t, std::vector<std::string>>>
Receiver::BucketizeItems(const std::vector<std::string>& queries,
                         size_t bucket_cnt) {
  std::unordered_map<size_t, std::vector<std::string>> bucket_item_map;

  for (const auto& query : queries) {
    int bucket_idx = std::hash<std::string>()(query) % bucket_cnt;

    if (bucket_item_map.find(bucket_idx) == bucket_item_map.end()) {
      bucket_item_map[bucket_idx] = std::vector<std::string>();
    }

    bucket_item_map[bucket_idx].emplace_back(query);
  }

  std::vector<std::pair<size_t, std::vector<std::string>>> res;
  res.reserve(bucket_item_map.size());

  for (const auto& it : bucket_item_map) {
    res.emplace_back(it.first, it.second);
  }

  return res;
}

std::vector<std::pair<size_t, std::vector<std::string>>>
Receiver::BucketizeItems(const std::string& query_file, size_t bucket_cnt) {
  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(query_file);

  return BucketizeItems(orig_items, bucket_cnt);
}

std::string Receiver::RequestOPRF(size_t bucket_idx) {
  oprf_receiver_ = std::make_unique<::apsi::oprf::OPRFReceiver>(items_vec_);

  unique_ptr<::apsi::network::SenderOperation> sop =
      psi::apsi_wrapper::Receiver::CreateOPRFRequest(*oprf_receiver_,
                                                     bucket_idx);
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.type = sop->type();
  stringstream ss;
  sop_header.save(ss);
  sop->save(ss);

  return ss.str();
}

std::string Receiver::RequestQuery(const std::string& oprf_response_str,
                                   size_t bucket_idx) {
  stringstream ss;
  ss << oprf_response_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
      make_unique<::apsi::network::SenderOperationResponseOPRF>();
  sop_response->load(ss);

  ::apsi::OPRFResponse response = ::apsi::to_oprf_response(sop_response);

  tie(oprf_items_, label_keys_) =
      psi::apsi_wrapper::Receiver::ExtractHashes(response, *oprf_receiver_);

  receiver_ = make_unique<psi::apsi_wrapper::Receiver>(*params_);
  auto query = receiver_->create_query(oprf_items_, bucket_idx);

  itt_ = query.second;

  sop_header.type = query.first->type();
  stringstream ss_out;
  sop_header.save(ss_out);
  query.first->save(ss_out);

  return ss_out.str();
}

size_t Receiver::ProcessResult(const std::string& query_response_str,
                               const std::string& output_file,
                               bool append_to_outfile) {
  stringstream ss;
  ss << query_response_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
      make_unique<::apsi::network::SenderOperationResponseQuery>();
  sop_response->load(ss);

  ::apsi::QueryResponse response = ::apsi::to_query_response(sop_response);
  auto seal_context = receiver_->get_seal_context();
  std::vector<::apsi::ResultPart> rps;
  while (rps.size() < response->package_count) {
    unique_ptr<::apsi::network::ResultPackage> rp(
        make_unique<::apsi::network::ResultPackage>());
    rp->load(ss, seal_context);
    rps.emplace_back(std::move(rp));
  }

  vector<::apsi::receiver::MatchRecord> query_result =
      receiver_->process_result(label_keys_, itt_, rps);

  return psi::apsi_wrapper::print_intersection_results(
      orig_items_, items_vec_, query_result, output_file, append_to_outfile);
}

}  // namespace psi::apsi_wrapper::api
