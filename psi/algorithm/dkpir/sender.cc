// Copyright 2025
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

#include "psi/algorithm/dkpir/sender.h"

#include "yacl/crypto/rand/rand.h"

namespace psi::dkpir {

namespace {
std::vector<unsigned char> ProcessQueries(
    gsl::span<const unsigned char> oprf_queries,
    const ::apsi::oprf::OPRFKey &oprf_key, const uint128_t &shuffle_seed,
    const uint64_t &shuffle_counter) {
  if (oprf_queries.size() % ::apsi::oprf::oprf_query_size) {
    throw std::invalid_argument("oprf_queries has invalid size");
  }

  STOPWATCH(sender_stopwatch, "OPRFSender::ProcessQueries");

  uint64_t query_count = oprf_queries.size() / ::apsi::oprf::oprf_query_size;
  std::vector<unsigned char> oprf_responses(query_count *
                                            ::apsi::oprf::oprf_response_size);

  auto oprf_in_ptr = oprf_queries.data();
  auto oprf_out_ptr = oprf_responses.data();

  ::apsi::ThreadPoolMgr tpm;
  uint64_t task_count =
      std::min<uint64_t>(::apsi::ThreadPoolMgr::GetThreadCount(), query_count);
  std::vector<std::future<void>> futures(task_count);

  yacl::crypto::YaclReplayUrbg<uint32_t> gen(shuffle_seed, shuffle_counter);
  std::vector<uint64_t> shuffle_indexes(query_count);
  for (uint64_t i = 0; i < query_count; ++i) {
    shuffle_indexes[i] = i;
  }
  std::shuffle(shuffle_indexes.begin(), shuffle_indexes.end(), gen);

  auto ProcessQueriesLambda = [&](uint64_t start_idx, uint64_t step) {
    for (uint64_t idx = start_idx; idx < query_count; idx += step) {
      // Load the point from input buffer
      ::apsi::oprf::ECPoint ecpt;
      ecpt.load(::apsi::oprf::ECPoint::point_save_span_const_type{
          oprf_in_ptr + idx * ::apsi::oprf::oprf_query_size,
          ::apsi::oprf::oprf_query_size});

      // Multiply with key
      if (!ecpt.scalar_multiply(oprf_key.key_span(), true)) {
        throw std::logic_error(
            "scalar multiplication failed due to invalid query data");
      }

      // Save the shuffled result to oprf_responses
      ecpt.save(::apsi::oprf::ECPoint::point_save_span_type{
          oprf_out_ptr +
              shuffle_indexes[idx] * ::apsi::oprf::oprf_response_size,
          ::apsi::oprf::oprf_response_size});
    }
  };

  for (uint64_t thread_idx = 0; thread_idx < task_count; thread_idx++) {
    futures[thread_idx] =
        tpm.thread_pool().enqueue(ProcessQueriesLambda, thread_idx, task_count);
  }

  for (auto &f : futures) {
    f.get();
  }

  return oprf_responses;
}
}  // namespace

::apsi::OPRFResponse DkPirSender::GenerateOPRFResponse(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key,
    const uint128_t &shuffle_seed, const uint64_t &shuffle_counter) {
  STOPWATCH(sender_stopwatch, "DkPirSender::RunOPRF");

  if (!oprf_request) {
    APSI_LOG_ERROR("Failed to process OPRF request: request is invalid");
    throw std::invalid_argument("request is invalid");
  }

  // OPRF response has the same size as the OPRF query
  ::apsi::OPRFResponse response_oprf =
      std::make_unique<::apsi::OPRFResponse::element_type>();

  if (oprf_request->data.empty()) {
    return response_oprf;
  }

  try {
    response_oprf->data =
        ProcessQueries(oprf_request->data, key, shuffle_seed, shuffle_counter);
  } catch (const std::exception &ex) {
    // Something was wrong with the OPRF request. This can mean malicious
    // data being sent to the sender in an attempt to extract OPRF key.
    // Best not to respond anything.
    APSI_LOG_ERROR("Processing OPRF request threw an exception: " << ex.what());
    return response_oprf;
  }

  return response_oprf;
}

void DkPirSender::RunOPRF(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key,
    const uint128_t &shuffle_seed, const uint64_t &shuffle_counter,
    ::apsi::network::Channel &chl,
    std::function<void(::apsi::network::Channel &, ::apsi::Response)>
        send_fun) {
  ::apsi::OPRFResponse response_oprf =
      GenerateOPRFResponse(oprf_request, key, shuffle_seed, shuffle_counter);

  if (response_oprf->data.empty()) {
    return;
  }

  try {
    send_fun(chl, std::move(response_oprf));
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Failed to send response to OPRF request; function threw an exception: "
        << ex.what());
    throw;
  }

  SPDLOG_INFO("Finished processing the OPRF query and shuffled the results");
}

void DkPirSender::RunQuery(
    const psi::dkpir::DkPirQuery &query, psi::apsi_wrapper::YaclChannel &chl,
    bool streaming_result,
    std::function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun,
    std::function<void(::apsi::network::Channel &, ::apsi::ResultPart)>
        send_rp_fun) {
  if (!query) {
    APSI_LOG_ERROR("Failed to process query request: query is invalid");
    throw std::invalid_argument("query is invalid");
  }

  // We use a custom SEAL memory that is freed after the query is done
  auto pool = ::seal::MemoryManager::GetPool(::seal::mm_force_new);

  ::apsi::ThreadPoolMgr tpm;

  auto send_func =
      psi::apsi_wrapper::Sender::BasicSend<::apsi::Response::element_type>;

  // Acquire read lock on sender_cnt_db
  auto sender_cnt_db = query.sender_cnt_db();
  if (sender_cnt_db == nullptr) {
    ::apsi::QueryResponse response_query =
        std::make_unique<::apsi::QueryResponse::element_type>();
    response_query->package_count = 0;
    try {
      send_func(chl, std::move(response_query));
    } catch (const std::exception &ex) {
      APSI_LOG_ERROR(
          "Failed to send response to query request; function threw an "
          "exception: "
          << ex.what());
      throw;
    }
    return;
  }

  auto sender_cnt_db_lock = sender_cnt_db->get_reader_lock();

  STOPWATCH(::apsi::util::sender_stopwatch, "DkPirSender::RunQuery");
  APSI_LOG_INFO("Start processing query request on database with "
                << sender_cnt_db->get_item_count() << " items");

  // Copy over the CryptoContext from ::apsi::sender::SenderDB; set the
  // Evaluator for this local instance. Relinearization keys may not have been
  // included in the query. In that case query.relin_keys() simply holds an
  // empty seal::RelinKeys instance. There is no problem with the below call to
  // CryptoContext::set_evaluator.
  ::apsi::CryptoContext crypto_context(sender_cnt_db->get_crypto_context());
  crypto_context.set_evaluator(query.relin_keys());

  // Get the PSIParams
  ::apsi::PSIParams params(sender_cnt_db->get_params());

  uint32_t bundle_idx_count = params.bundle_idx_count();
  uint32_t max_items_per_bin = params.table_params().max_items_per_bin;

  // Extract the PowersDag
  ::apsi::PowersDag pd = query.pd();

  // The query response only tells how many ResultPackages to expect; send this
  // first
  uint32_t package_count =
      ::seal::util::safe_cast<uint32_t>(sender_cnt_db->get_bin_bundle_count());
  ::apsi::QueryResponse response_query =
      std::make_unique<::apsi::QueryResponse::element_type>();
  response_query->package_count = package_count;

  if (streaming_result) {
    try {
      send_fun(chl, std::move(response_query));
    } catch (const std::exception &ex) {
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
  std::vector<psi::apsi_wrapper::CiphertextPowers> all_powers(bundle_idx_count);

  // Initialize powers
  for (psi::apsi_wrapper::CiphertextPowers &powers : all_powers) {
    // The + 1 is because we index by power. The 0th power is a dummy value. I
    // promise this makes things easier to read.
    uint64_t powers_size = static_cast<uint64_t>(max_items_per_bin) + 1;
    powers.reserve(powers_size);
    for (uint64_t i = 0; i < powers_size; i++) {
      powers.emplace_back(pool);
    }
  }

  // Load inputs provided in the query
  for (auto &q : query.data()) {
    // The exponent of all the query powers we're about to iterate through
    uint64_t exponent = static_cast<uint64_t>(q.first);

    // Load Qᵢᵉ for all bundle indices i, where e is the exponent specified
    // above
    for (uint64_t bundle_idx = 0; bundle_idx < all_powers.size();
         bundle_idx++) {
      // Load input^power to all_powers[bundle_idx][exponent]
      APSI_LOG_DEBUG("Extracting query ciphertext power "
                     << exponent << " for bundle index " << bundle_idx);
      all_powers[bundle_idx][exponent] = std::move(q.second[bundle_idx]);
    }
  }

  // Compute query powers for the bundle indexes
  for (uint64_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
    psi::apsi_wrapper::Sender::ComputePowers(
        sender_cnt_db, crypto_context, all_powers, pd,
        static_cast<uint32_t>(bundle_idx), pool);
  }

  APSI_LOG_DEBUG("Finished computing powers for all bundle indices");
  APSI_LOG_DEBUG("Start processing bin bundle caches");

  std::vector<std::future<void>> futures;
  std::vector<::apsi::ResultPart> rps;
  std::mutex rps_mutex;

  for (uint64_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
    auto bundle_caches =
        sender_cnt_db->get_cache_at(static_cast<uint32_t>(bundle_idx));
    for (auto &cache : bundle_caches) {
      futures.push_back(tpm.thread_pool().enqueue([&, bundle_idx, cache]() {
        psi::apsi_wrapper::Sender::ProcessBinBundleCache(
            sender_cnt_db, crypto_context, cache, all_powers, chl, send_rp_fun,
            static_cast<uint32_t>(bundle_idx), query.compr_mode(), pool,
            rps_mutex, streaming_result ? nullptr : &rps);
      }));
    }
  }

  // Wait until all bin bundle caches have been processed
  for (auto &f : futures) {
    f.get();
  }

  if (!streaming_result) {
    APSI_LOG_INFO("streaming_result is off.");
    try {
      send_fun(chl, std::move(response_query));
    } catch (const std::exception &ex) {
      APSI_LOG_ERROR(
          "Failed to send response to query request; function threw an "
          "exception: "
          << ex.what());
      throw;
    }

    for (auto &rp : rps) {
      try {
        send_rp_fun(chl, std::move(rp));
      } catch (const std::exception &ex) {
        APSI_LOG_ERROR(
            "Failed to send result part; function threw an exception:"
            << ex.what());
        throw;
      }
    }
  }

  APSI_LOG_INFO("Finished processing query request");
}
}  // namespace psi::dkpir