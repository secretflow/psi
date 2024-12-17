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

#include "psi/wrapper/apsi/api/sender.h"

#include <cstddef>
#include <filesystem>

#include "apsi/network/stream_channel.h"

#include "psi/utils/multiplex_disk_cache.h"
#include "psi/wrapper/apsi/sender.h"
#include "psi/wrapper/apsi/utils/common.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

using namespace std;

namespace psi::apsi_wrapper::api {

void Sender::SetThreadCount(size_t threads) { thread_count_ = threads; }

bool Sender::GenerateSenderDb() {
  GenerateGroupBucketDB(group_db_, thread_count_);

  return true;
}

GroupDBItem::BucketDBItem Sender::GetDefaultDB() {
  for (size_t i = 0; i < group_db_.GetBucketNum(); i++) {
    auto db = group_db_.GetBucketDB(i);
    if (db.sender_db) {
      return db;
    }
  }
  YACL_THROW("no valid db found");
}

std::string Sender::GenerateParams() {
  YACL_ENFORCE(group_db_.IsDBGenerated(), "group_db is not generated");

  auto default_db = GetDefaultDB();

  ::apsi::ParamsResponse response_params =
      make_unique<::apsi::ParamsResponse::element_type>();
  response_params->params =
      make_unique<::apsi::PSIParams>(default_db.sender_db->get_params());

  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.type = response_params->type();

  stringstream ss;

  sop_header.save(ss);
  response_params->save(ss);

  return ss.str();
}

std::string Sender::RunOPRF(const std::string &oprf_request_str) {
  YACL_ENFORCE(group_db_.IsDBGenerated(), "group_db is not generated");

  stringstream ss;
  ss << oprf_request_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  unique_ptr<::apsi::network::SenderOperation> sop =
      make_unique<::apsi::network::SenderOperationOPRF>();
  sop->load(ss);

  ::apsi::OPRFRequest oprf_request = ::apsi::to_oprf_request(std::move(sop));

  auto db = group_db_.GetBucketDB(oprf_request->bucket_idx);

  ::apsi::OPRFResponse response =
      ::psi::apsi_wrapper::Sender::GenerateOPRFResponse(oprf_request,
                                                        db.oprf_key);

  sop_header.type = response->type();

  stringstream ss_out;

  sop_header.save(ss_out);
  response->save(ss_out);

  return ss_out.str();
}

std::string Sender::RunQuery(const std::string &query_str) {
  YACL_ENFORCE(group_db_.IsDBGenerated(), "group_db is not generated");

  stringstream ss;
  ss << query_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  unique_ptr<::apsi::network::SenderOperation> sop =
      make_unique<::apsi::network::SenderOperationQuery>();

  auto default_db = GetDefaultDB();

  sop->load(ss, default_db.sender_db->get_seal_context());

  auto query_request = ::apsi::to_query_request(std::move(sop));

  auto db = group_db_.GetBucketDB(query_request->bucket_idx);

  if (db.sender_db == nullptr) {
    return "";
  }

  apsi::sender::Query query(std::move(query_request), db.sender_db);

  {
    // the following is copied from Sender::RunQuery
    // We use a custom SEAL memory that is freed after the query is done
    auto pool = ::seal::MemoryManager::GetPool(::seal::mm_force_new);

    ::apsi::ThreadPoolMgr tpm;

    // Acquire read lock on ::apsi::sender::SenderDB
    auto sender_db = query.sender_db();
    auto sender_db_lock = sender_db->get_reader_lock();

    // Copy over the CryptoContext from ::apsi::sender::SenderDB; set the
    // Evaluator for this local instance. Relinearization keys may not have been
    // included in the query. In that case query.relin_keys() simply holds an
    // empty seal::RelinKeys instance. There is no problem with the below call
    // to CryptoContext::set_evaluator.
    ::apsi::CryptoContext crypto_context(sender_db->get_crypto_context());
    crypto_context.set_evaluator(query.relin_keys());

    // Get the PSIParams
    ::apsi::PSIParams params(sender_db->get_params());

    uint32_t bundle_idx_count = params.bundle_idx_count();
    uint32_t max_items_per_bin = params.table_params().max_items_per_bin;

    // Extract the PowersDag
    ::apsi::PowersDag pd = query.pd();

    // The query response only tells how many ResultPackages to expect; send
    // this first
    uint32_t package_count =
        ::seal::util::safe_cast<uint32_t>(sender_db->get_bin_bundle_count());
    ::apsi::QueryResponse response_query =
        make_unique<::apsi::QueryResponse::element_type>();
    response_query->package_count = package_count;

    // For each bundle index i, we need a vector of powers of the query Qᵢ. We
    // need powers all the way up to Qᵢ^max_items_per_bin. We don't store the
    // zeroth power. If Paterson-Stockmeyer is used, then only a subset of the
    // powers will be populated.
    vector<CiphertextPowers> all_powers(bundle_idx_count);

    // Initialize powers
    for (CiphertextPowers &powers : all_powers) {
      // The + 1 is because we index by power. The 0th power is a dummy value. I
      // promise this makes things easier to read.
      size_t powers_size = static_cast<size_t>(max_items_per_bin) + 1;
      powers.reserve(powers_size);
      for (size_t i = 0; i < powers_size; i++) {
        powers.emplace_back(pool);
      }
    }

    // Load inputs provided in the query
    for (auto &q : query.data()) {
      // The exponent of all the query powers we're about to iterate through
      size_t exponent = static_cast<size_t>(q.first);

      // Load Qᵢᵉ for all bundle indices i, where e is the exponent specified
      // above
      for (size_t bundle_idx = 0; bundle_idx < all_powers.size();
           bundle_idx++) {
        // Load input^power to all_powers[bundle_idx][exponent]
        all_powers[bundle_idx][exponent] = std::move(q.second[bundle_idx]);
      }
    }

    // Compute query powers for the bundle indexes
    for (size_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
      ::psi::apsi_wrapper::Sender::ComputePowers(
          sender_db, crypto_context, all_powers, pd,
          static_cast<uint32_t>(bundle_idx), pool);
    }

    vector<future<void>> futures;
    std::vector<::apsi::ResultPart> rps;
    std::mutex rps_mutex;

    stringstream ss_c;
    ::apsi::network::StreamChannel chl(ss_c);

    for (size_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
      auto bundle_caches =
          sender_db->get_cache_at(static_cast<uint32_t>(bundle_idx));
      for (auto &cache : bundle_caches) {
        futures.push_back(tpm.thread_pool().enqueue([&, bundle_idx, cache]() {
          ::psi::apsi_wrapper::Sender::ProcessBinBundleCache(
              sender_db, crypto_context, cache, all_powers, chl,
              ::psi::apsi_wrapper::Sender::BasicSend<
                  ::apsi::ResultPart::element_type>,
              static_cast<uint32_t>(bundle_idx), query.compr_mode(), pool,
              rps_mutex, &rps);
        }));
      }
    }

    // Wait until all bin bundle caches have been processed
    for (auto &f : futures) {
      f.get();
    }

    stringstream ss_out;
    ::apsi::network::SenderOperationHeader sop_header;
    sop_header.type = response_query->type();

    sop_header.save(ss_out);
    response_query->save(ss_out);

    for (auto &rp : rps) {
      rp->save(ss_out);
    }

    return ss_out.str();
  }
}

std::vector<std::string> Sender::RunOPRF(
    const std::vector<std::string> &oprf_request_str) {
  std::vector<std::string> oprf_response;
  for (const auto &oprf_request : oprf_request_str) {
    oprf_response.emplace_back(RunOPRF(oprf_request));
  }
  return oprf_response;
}

std::vector<std::string> Sender::RunQuery(
    const std::vector<std::string> &query_str) {
  std::vector<std::string> query_response;
  for (const auto &query : query_str) {
    query_response.emplace_back(RunQuery(query));
  }
  return query_response;
}

}  // namespace psi::apsi_wrapper::api
