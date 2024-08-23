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

#include "psi/apsi_wrapper/sender.h"

// STD
#include <future>
#include <sstream>

// APSI
#include "apsi/crypto_context.h"
#include "apsi/log.h"
#include "apsi/network/channel.h"
#include "apsi/network/result_package.h"
#include "apsi/psi_params.h"
#include "apsi/seal_object.h"
#include "apsi/thread_pool_mgr.h"
#include "apsi/util/stopwatch.h"
#include "apsi/util/utils.h"

// SEAL
#include "seal/evaluator.h"
#include "seal/modulus.h"
#include "seal/util/common.h"

using namespace std;

namespace psi::apsi_wrapper {

void Sender::RunParams(
    const ::apsi::ParamsRequest &params_request,
    shared_ptr<::apsi::sender::SenderDB> sender_db,
    ::apsi::network::Channel &chl,
    function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun) {
  STOPWATCH(sender_stopwatch, "Sender::RunParams");

  if (!params_request) {
    APSI_LOG_ERROR("Failed to process parameter request: request is invalid");
    throw invalid_argument("request is invalid");
  }

  // Check that the database is set
  if (!sender_db) {
    throw logic_error("::apsi::sender::SenderDB is not set");
  }

  APSI_LOG_INFO("Start processing parameter request");

  ::apsi::ParamsResponse response_params =
      make_unique<::apsi::ParamsResponse::element_type>();
  response_params->params =
      make_unique<::apsi::PSIParams>(sender_db->get_params());

  try {
    send_fun(chl, std::move(response_params));
  } catch (const exception &ex) {
    APSI_LOG_ERROR(
        "Failed to send response to parameter request; function threw an "
        "exception: "
        << ex.what());
    throw;
  }

  APSI_LOG_INFO("Finished processing parameter request");
}

::apsi::OPRFResponse Sender::GenerateOPRFResponse(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key) {
  STOPWATCH(sender_stopwatch, "Sender::RunOPRF");

  if (!oprf_request) {
    APSI_LOG_ERROR("Failed to process OPRF request: request is invalid");
    throw invalid_argument("request is invalid");
  }

  APSI_LOG_INFO("Start processing OPRF request for "
                << oprf_request->data.size() / ::apsi::oprf::oprf_query_size
                << " items");

  // OPRF response has the same size as the OPRF query
  ::apsi::OPRFResponse response_oprf =
      make_unique<::apsi::OPRFResponse::element_type>();

  if (oprf_request->data.empty()) {
    return response_oprf;
  }

  try {
    response_oprf->data =
        ::apsi::oprf::OPRFSender::ProcessQueries(oprf_request->data, key);
  } catch (const exception &ex) {
    // Something was wrong with the OPRF request. This can mean malicious
    // data being sent to the sender in an attempt to extract OPRF key.
    // Best not to respond anything.
    APSI_LOG_ERROR("Processing OPRF request threw an exception: " << ex.what());
    return response_oprf;
  }

  return response_oprf;
}

void Sender::RunOPRF(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key,
    ::apsi::network::Channel &chl,
    function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun) {
  ::apsi::OPRFResponse response_oprf = GenerateOPRFResponse(oprf_request, key);

  if (response_oprf->data.empty()) {
    return;
  }

  try {
    send_fun(chl, std::move(response_oprf));
  } catch (const exception &ex) {
    APSI_LOG_ERROR(
        "Failed to send response to OPRF request; function threw an exception: "
        << ex.what());
    throw;
  }

  APSI_LOG_INFO("Finished processing OPRF request");
}

void Sender::RunQuery(
    const ::apsi::sender::Query &query, ::apsi::network::Channel &chl,
    bool streaming_result,
    function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun,
    function<void(::apsi::network::Channel &, ::apsi::ResultPart)>
        send_rp_fun) {
  if (!query) {
    APSI_LOG_ERROR("Failed to process query request: query is invalid");
    throw invalid_argument("query is invalid");
  }

  // We use a custom SEAL memory that is freed after the query is done
  auto pool = ::seal::MemoryManager::GetPool(::seal::mm_force_new);

  ::apsi::ThreadPoolMgr tpm;

  auto send_func = BasicSend<::apsi::Response::element_type>;

  // Acquire read lock on ::apsi::sender::SenderDB
  auto sender_db = query.sender_db();
  if (sender_db == nullptr) {
    ::apsi::QueryResponse response_query =
        make_unique<::apsi::QueryResponse::element_type>();
    response_query->package_count = 0;
    try {
      send_func(chl, std::move(response_query));
    } catch (const exception &ex) {
      APSI_LOG_ERROR(
          "Failed to send response to query request; function threw an "
          "exception: "
          << ex.what());
      throw;
    }
    return;
  }

  auto sender_db_lock = sender_db->get_reader_lock();

  STOPWATCH(::apsi::util::sender_stopwatch, "Sender::RunQuery");
  APSI_LOG_INFO("Start processing query request on database with "
                << sender_db->get_item_count() << " items");

  // Copy over the CryptoContext from ::apsi::sender::SenderDB; set the
  // Evaluator for this local instance. Relinearization keys may not have been
  // included in the query. In that case query.relin_keys() simply holds an
  // empty seal::RelinKeys instance. There is no problem with the below call to
  // CryptoContext::set_evaluator.
  ::apsi::CryptoContext crypto_context(sender_db->get_crypto_context());
  crypto_context.set_evaluator(query.relin_keys());

  // Get the PSIParams
  ::apsi::PSIParams params(sender_db->get_params());

  uint32_t bundle_idx_count = params.bundle_idx_count();
  uint32_t max_items_per_bin = params.table_params().max_items_per_bin;

  // Extract the PowersDag
  ::apsi::PowersDag pd = query.pd();

  // The query response only tells how many ResultPackages to expect; send this
  // first
  uint32_t package_count =
      ::seal::util::safe_cast<uint32_t>(sender_db->get_bin_bundle_count());
  ::apsi::QueryResponse response_query =
      make_unique<::apsi::QueryResponse::element_type>();
  response_query->package_count = package_count;

  if (streaming_result) {
    try {
      send_fun(chl, std::move(response_query));
    } catch (const exception &ex) {
      APSI_LOG_ERROR(
          "Failed to send response to query request; function threw an "
          "exception: "
          << ex.what());
      throw;
    }
  }

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
    for (size_t bundle_idx = 0; bundle_idx < all_powers.size(); bundle_idx++) {
      // Load input^power to all_powers[bundle_idx][exponent]
      APSI_LOG_DEBUG("Extracting query ciphertext power "
                     << exponent << " for bundle index " << bundle_idx);
      all_powers[bundle_idx][exponent] = std::move(q.second[bundle_idx]);
    }
  }

  // Compute query powers for the bundle indexes
  for (size_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
    ComputePowers(sender_db, crypto_context, all_powers, pd,
                  static_cast<uint32_t>(bundle_idx), pool);
  }

  APSI_LOG_DEBUG("Finished computing powers for all bundle indices");
  APSI_LOG_DEBUG("Start processing bin bundle caches");

  vector<future<void>> futures;
  std::vector<::apsi::ResultPart> rps;
  std::mutex rps_mutex;

  for (size_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
    auto bundle_caches =
        sender_db->get_cache_at(static_cast<uint32_t>(bundle_idx));
    for (auto &cache : bundle_caches) {
      futures.push_back(tpm.thread_pool().enqueue([&, bundle_idx, cache]() {
        ProcessBinBundleCache(sender_db, crypto_context, cache, all_powers, chl,
                              send_rp_fun, static_cast<uint32_t>(bundle_idx),
                              query.compr_mode(), pool, rps_mutex,
                              streaming_result ? nullptr : &rps);
      }));
    }
  }

  // Wait until all bin bundle caches have been processed
  for (auto &f : futures) {
    f.get();
  }

  if (!streaming_result) {
    APSI_LOG_INFO("streaming_result is off.")
    try {
      send_fun(chl, std::move(response_query));
    } catch (const exception &ex) {
      APSI_LOG_ERROR(
          "Failed to send response to query request; function threw an "
          "exception: "
          << ex.what());
      throw;
    }

    for (auto &rp : rps) {
      try {
        send_rp_fun(chl, std::move(rp));
      } catch (const exception &ex) {
        APSI_LOG_ERROR(
            "Failed to send result part; function threw an exception:"
            << ex.what());
        throw;
      }
    }
  }

  APSI_LOG_INFO("Finished processing query request");
}

void Sender::ComputePowers(
    const shared_ptr<::apsi::sender::SenderDB> &sender_db,
    const ::apsi::CryptoContext &crypto_context,
    vector<CiphertextPowers> &all_powers, const ::apsi::PowersDag &pd,
    uint32_t bundle_idx, ::seal::MemoryPoolHandle &pool) {
  STOPWATCH(sender_stopwatch, "Sender::ComputePowers");
  auto bundle_caches = sender_db->get_cache_at(bundle_idx);
  if (!bundle_caches.size()) {
    return;
  }

  // Compute all powers of the query
  APSI_LOG_DEBUG("Computing all query ciphertext powers for bundle index "
                 << bundle_idx);

  auto evaluator = crypto_context.evaluator();
  auto relin_keys = crypto_context.relin_keys();

  CiphertextPowers &powers_at_this_bundle_idx = all_powers[bundle_idx];
  bool relinearize = crypto_context.seal_context()->using_keyswitching();
  pd.parallel_apply([&](const ::apsi::PowersDag::PowersNode &node) {
    if (!node.is_source()) {
      auto parents = node.parents;
      ::seal::Ciphertext prod(pool);
      if (parents.first == parents.second) {
        evaluator->square(powers_at_this_bundle_idx[parents.first], prod, pool);
      } else {
        evaluator->multiply(powers_at_this_bundle_idx[parents.first],
                            powers_at_this_bundle_idx[parents.second], prod,
                            pool);
      }
      if (relinearize) {
        evaluator->relinearize_inplace(prod, *relin_keys, pool);
      }
      powers_at_this_bundle_idx[node.power] = std::move(prod);
    }
  });

  // Now that all powers of the ciphertext have been computed, we need to
  // transform them to NTT form. This will substantially improve the polynomial
  // evaluation, because the plaintext polynomials are already in NTT
  // transformed form, and the ciphertexts are used repeatedly for each bin
  // bundle at this index. This computation is separate from the graph
  // processing above, because the multiplications must all be done before
  // transforming to NTT form. We omit the first ciphertext in the vector,
  // because it corresponds to the zeroth power of the query and is included
  // only for convenience of the indexing; the ciphertext is actually not
  // set or valid for use.

  ::apsi::ThreadPoolMgr tpm;

  // After computing all powers we will modulus switch down to parameters that
  // one more level for low powers than for high powers; same choice must be
  // used when encoding/NTT transforming the ::apsi::sender::SenderDB data.
  auto high_powers_parms_id =
      get_parms_id_for_chain_idx(*crypto_context.seal_context(), 1);
  auto low_powers_parms_id =
      get_parms_id_for_chain_idx(*crypto_context.seal_context(), 2);

  uint32_t ps_low_degree = sender_db->get_params().query_params().ps_low_degree;

  vector<future<void>> futures;
  for (uint32_t power : pd.target_powers()) {
    futures.push_back(tpm.thread_pool().enqueue([&, power]() {
      if (!ps_low_degree) {
        // Only one ciphertext-plaintext multiplication is needed after this
        evaluator->mod_switch_to_inplace(powers_at_this_bundle_idx[power],
                                         high_powers_parms_id, pool);

        // All powers must be in NTT form
        evaluator->transform_to_ntt_inplace(powers_at_this_bundle_idx[power]);
      } else {
        if (power <= ps_low_degree) {
          // Low powers must be at a higher level than high powers
          evaluator->mod_switch_to_inplace(powers_at_this_bundle_idx[power],
                                           low_powers_parms_id, pool);

          // Low powers must be in NTT form
          evaluator->transform_to_ntt_inplace(powers_at_this_bundle_idx[power]);
        } else {
          // High powers are only modulus switched
          evaluator->mod_switch_to_inplace(powers_at_this_bundle_idx[power],
                                           high_powers_parms_id, pool);
        }
      }
    }));
  }

  for (auto &f : futures) {
    f.get();
  }
}

void Sender::ProcessBinBundleCache(
    const shared_ptr<::apsi::sender::SenderDB> &sender_db,
    const ::apsi::CryptoContext &crypto_context,
    reference_wrapper<const ::apsi::sender::BinBundleCache> cache,
    vector<CiphertextPowers> &all_powers, ::apsi::network::Channel &chl,
    function<void(::apsi::network::Channel &, ::apsi::ResultPart)> send_rp_fun,
    uint32_t bundle_idx, ::seal::compr_mode_type compr_mode,
    ::seal::MemoryPoolHandle &pool, std::mutex &rps_mutex,
    std::vector<::apsi::ResultPart> *rps) {
  STOPWATCH(sender_stopwatch, "Sender::ProcessBinBundleCache");

  // Package for the result data
  auto rp = make_unique<::apsi::network::ResultPackage>();
  rp->compr_mode = compr_mode;

  rp->bundle_idx = bundle_idx;
  rp->nonce_byte_count =
      ::seal::util::safe_cast<uint32_t>(sender_db->get_nonce_byte_count());
  rp->label_byte_count =
      ::seal::util::safe_cast<uint32_t>(sender_db->get_label_byte_count());

  // Compute the matching result and move to rp
  const ::apsi::sender::BatchedPlaintextPolyn &matching_polyn =
      cache.get().batched_matching_polyn;

  // Determine if we use Paterson-Stockmeyer or not
  uint32_t ps_low_degree = sender_db->get_params().query_params().ps_low_degree;
  uint32_t degree =
      ::seal::util::safe_cast<uint32_t>(matching_polyn.batched_coeffs.size()) -
      1;
  bool using_ps = (ps_low_degree > 1) && (ps_low_degree < degree);
  if (using_ps) {
    rp->psi_result = matching_polyn.eval_patstock(
        crypto_context, all_powers[bundle_idx],
        ::seal::util::safe_cast<size_t>(ps_low_degree), pool);
  } else {
    rp->psi_result = matching_polyn.eval(all_powers[bundle_idx], pool);
  }

  for (const auto &interp_polyn : cache.get().batched_interp_polyns) {
    // Compute the label result and move to rp
    degree =
        ::seal::util::safe_cast<uint32_t>(interp_polyn.batched_coeffs.size()) -
        1;
    using_ps = (ps_low_degree > 1) && (ps_low_degree < degree);
    if (using_ps) {
      rp->label_result.push_back(interp_polyn.eval_patstock(
          crypto_context, all_powers[bundle_idx], ps_low_degree, pool));
    } else {
      rp->label_result.push_back(
          interp_polyn.eval(all_powers[bundle_idx], pool));
    }
  }

  if (rps != nullptr) {
    std::lock_guard<std::mutex> lock(rps_mutex);
    rps->emplace_back(std::move(rp));
  } else {
    // Send this result part
    try {
      send_rp_fun(chl, std::move(rp));
    } catch (const exception &ex) {
      APSI_LOG_ERROR("Failed to send result part; function threw an exception:"
                     << ex.what());
      throw;
    }
  }
}
}  // namespace psi::apsi_wrapper
