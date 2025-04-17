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

#include <fstream>

#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/dkpir/common.h"
#include "psi/algorithm/dkpir/encryptor.h"
#include "psi/algorithm/dkpir/sender_cnt_db.h"
#include "psi/utils/csv_converter.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

using namespace std::chrono_literals;

namespace psi::dkpir {
namespace {
std::vector<unsigned char> ProcessQueries(
    gsl::span<const unsigned char> oprf_queries,
    const ::apsi::oprf::OPRFKey &oprf_key, uint128_t shuffle_seed,
    uint64_t shuffle_counter) {
  YACL_ENFORCE((oprf_queries.size() % ::apsi::oprf::oprf_query_size) == 0,
               "oprf_queries has invalid size");

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
        YACL_THROW("scalar multiplication failed due to invalid query data");
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

DkPirSender::DkPirSender(const DkPirSenderOptions &options)
    : options_(options),
      sender_db_(nullptr),
      sender_cnt_db_(nullptr),
      shuffle_seed_(yacl::crypto::SecureRandSeed()),
      shuffle_counter_(yacl::crypto::SecureRandU64()),
      query_count_(0) {}

void DkPirSender::PreProcessData(const std::string &key_value_file,
                                 const std::string &key_count_file) {
  psi::ApsiCsvConverter sender_converter(options_.source_file, options_.key,
                                         options_.labels);

  if (options_.skip_count_check) {
    sender_converter.MergeColumnAndRow(key_value_file);
    SPDLOG_INFO("Sender created a temporary file for the data table");
  } else {
    sender_converter.MergeColumnAndRow(key_value_file, key_count_file);
    SPDLOG_INFO(
        "Sender created two temporary files, one for the data table and the "
        "other for the row count table");
  }
}
void DkPirSender::GenerateDB(const std::string &key_value_file,
                             const std::string &key_count_file) {
  ::apsi::oprf::OPRFKey oprf_key;
  std::shared_ptr<::apsi::sender::SenderDB> sender_db;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db;

  // Generate SenderDB (for data)
  sender_db = psi::apsi_wrapper::GenerateSenderDB(
      key_value_file, options_.params_file, options_.nonce_byte_count,
      options_.compress, oprf_key);
  YACL_ENFORCE(sender_db != nullptr, "Create sender_db from {} failed",
               key_value_file);

  // Save the sender_db if a save file was given
  YACL_ENFORCE(psi::apsi_wrapper::TrySaveSenderDB(options_.value_sdb_out_file,
                                                  sender_db, oprf_key),
               "Save sender_db to {} failed.", options_.value_sdb_out_file);
  SPDLOG_INFO("Sender saved sender_db");

  if (!options_.skip_count_check) {
    // Generate SenderCntDB (for row count)
    sender_cnt_db =
        GenerateSenderCntDB(key_count_file, options_.params_file,
                            options_.secret_key_file, options_.nonce_byte_count,
                            options_.compress, options_.curve_type, oprf_key);
    YACL_ENFORCE(sender_cnt_db != nullptr,
                 "Create sender_cnt_db from {} failed", key_count_file);

    // Save the sender_cnt_db using the reusable method TrySaveSenderDB
    YACL_ENFORCE(psi::apsi_wrapper::TrySaveSenderDB(options_.count_sdb_out_file,
                                                    sender_cnt_db, oprf_key),
                 "Save sender_cnt_db to {} failed.",
                 options_.count_sdb_out_file);

    SPDLOG_INFO("Sender saved sender_cnt_db");
  }
}

void DkPirSender::LoadDB() {
  sender_db_ = psi::apsi_wrapper::TryLoadSenderDB(
      options_.value_sdb_out_file, options_.params_file, oprf_key_);
  YACL_ENFORCE(sender_db_ != nullptr, "Load old sender_db from {} failed",
               options_.value_sdb_out_file);

  SPDLOG_INFO("Sender loaded sender_db");

  if (!options_.skip_count_check) {
    // Here, we reuse the method TryLoadSenderDB, which means that oprf_key will
    // be read twice. But since these two databases actually use the same
    // oprf_key, it doesn't matter.
    sender_cnt_db_ = psi::apsi_wrapper::TryLoadSenderDB(
        options_.count_sdb_out_file, options_.params_file, oprf_key_);

    YACL_ENFORCE(sender_cnt_db_ != nullptr,
                 "Load old sender_cnt_db from {} failed",
                 options_.count_sdb_out_file);

    SPDLOG_INFO("Sender loaded sender_cnt_db");
  }
}

void DkPirSender::LoadSecretKey() {
  yacl::math::MPInt x;
  polynomial_.resize(2);

  std::ifstream fs(options_.secret_key_file, std::ios::binary);
  psi::dkpir::Load(polynomial_, x, fs);
  fs.close();

  std::string curve_name = FetchCurveName(options_.curve_type);
  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(curve_name);

  secret_key_ = heu::lib::algorithms::elgamal::SecretKey(x, curve);
  public_key_ =
      heu::lib::algorithms::elgamal::PublicKey(curve, curve->MulBase(x));
}

::apsi::Request DkPirSender::ReceiveRequest(
    psi::apsi_wrapper::YaclChannel &chl) {
  auto seal_context = sender_db_->get_seal_context();
  std::unique_ptr<::apsi::network::SenderOperation> sop;
  bool logged_waiting = false;

  while (!(sop = chl.receive_operation(seal_context))) {
    if (!logged_waiting) {
      // We want to log 'Waiting' only once, even if we have to wait
      // for several sleeps. And only once after processing a request as
      // well.
      logged_waiting = true;
      APSI_LOG_INFO("Waiting for request from Receiver");
    }

    std::this_thread::sleep_for(50ms);
  }

  return sop;
}

::apsi::OPRFResponse DkPirSender::RunOPRF(::apsi::Request request) {
  // Extract the OPRF request
  ::apsi::OPRFRequest oprf_request =
      ::apsi::to_oprf_request(std::move(request));

  if (!oprf_request) {
    APSI_LOG_ERROR("Failed to process OPRF request: request is invalid");
    YACL_THROW("request is invalid");
  }

  query_count_ = oprf_request->data.size() / ::apsi::oprf::oprf_query_size;
  SPDLOG_INFO("Receiver queried a total of {} keys", query_count_);

  if (options_.skip_count_check) {
    return psi::apsi_wrapper::Sender::GenerateOPRFResponse(oprf_request,
                                                           oprf_key_);
  }

  ::apsi::OPRFResponse response_oprf =
      std::make_unique<::apsi::OPRFResponse::element_type>();

  if (oprf_request->data.empty()) {
    return response_oprf;
  }

  try {
    response_oprf->data = ProcessQueries(oprf_request->data, oprf_key_,
                                         shuffle_seed_, shuffle_counter_);
  } catch (const std::exception &ex) {
    // Something was wrong with the OPRF request. This can mean malicious
    // data being sent to the sender in an attempt to extract OPRF key.
    // Best not to respond anything.
    APSI_LOG_ERROR("Processing OPRF request threw an exception: " << ex.what());
    throw;
    return response_oprf;
  }

  return response_oprf;
}

void DkPirSender::RunQuery(
    const psi::dkpir::DkPirQuery &query, psi::apsi_wrapper::YaclChannel &chl,
    bool is_count_query,
    std::function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun,
    std::function<void(::apsi::network::Channel &, ::apsi::ResultPart)>
        send_rp_fun) {
  if (!query) {
    APSI_LOG_ERROR("Failed to process query request: query is invalid");
    YACL_THROW("query is invalid");
  }

  // We use a custom SEAL memory that is freed after the query is done
  auto pool = ::seal::MemoryManager::GetPool(::seal::mm_force_new);

  ::apsi::ThreadPoolMgr tpm;

  auto send_func =
      psi::apsi_wrapper::Sender::BasicSend<::apsi::Response::element_type>;

  // Determine whether to perform a count query based on is_count_query
  std::shared_ptr<::apsi::sender::SenderDB> db;
  if (is_count_query) {
    db = query.sender_cnt_db();
  } else {
    db = query.sender_db();
  }

  if (db == nullptr) {
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

  // Acquire read lock
  auto db_lock = db->get_reader_lock();

  STOPWATCH(::apsi::util::sender_stopwatch, "DkPirSender::RunQuery");
  APSI_LOG_INFO("Start processing query request on database with "
                << db->get_item_count() << " items");

  // Copy over the CryptoContext from ::apsi::sender::SenderDB; set the
  // Evaluator for this local instance. Relinearization keys may not have been
  // included in the query. In that case query.relin_keys() simply holds an
  // empty seal::RelinKeys instance. There is no problem with the below call to
  // CryptoContext::set_evaluator.
  ::apsi::CryptoContext crypto_context(db->get_crypto_context());
  crypto_context.set_evaluator(query.relin_keys());

  // Get the PSIParams
  ::apsi::PSIParams params(db->get_params());

  uint32_t bundle_idx_count = params.bundle_idx_count();
  uint32_t max_items_per_bin = params.table_params().max_items_per_bin;

  // Extract the PowersDag
  ::apsi::PowersDag pd = query.pd();

  // The query response only tells how many ResultPackages to expect; send this
  // first
  uint32_t package_count =
      ::seal::util::safe_cast<uint32_t>(db->get_bin_bundle_count());
  ::apsi::QueryResponse response_query =
      std::make_unique<::apsi::QueryResponse::element_type>();
  response_query->package_count = package_count;

  if (options_.streaming_result) {
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
    psi::apsi_wrapper::Sender::ComputePowers(db, crypto_context, all_powers, pd,
                                             static_cast<uint32_t>(bundle_idx),
                                             pool);
  }

  APSI_LOG_DEBUG("Finished computing powers for all bundle indices");
  APSI_LOG_DEBUG("Start processing bin bundle caches");

  std::vector<std::future<void>> futures;
  std::vector<::apsi::ResultPart> rps;
  std::mutex rps_mutex;

  for (uint64_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
    auto bundle_caches = db->get_cache_at(static_cast<uint32_t>(bundle_idx));
    for (auto &cache : bundle_caches) {
      futures.push_back(tpm.thread_pool().enqueue([&, bundle_idx, cache]() {
        psi::apsi_wrapper::Sender::ProcessBinBundleCache(
            db, crypto_context, cache, all_powers, chl, send_rp_fun,
            static_cast<uint32_t>(bundle_idx), query.compr_mode(), pool,
            rps_mutex, options_.streaming_result ? nullptr : &rps);
      }));
    }
  }

  // Wait until all bin bundle caches have been processed
  for (auto &f : futures) {
    f.get();
  }

  if (!options_.streaming_result) {
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

heu::lib::algorithms::elgamal::Ciphertext DkPirSender::ReceiveRowCountCt(
    std::shared_ptr<yacl::link::Context> lctx) {
  auto curve = public_key_.GetCurve();

  yacl::Buffer row_count_ct_buf =
      lctx->Recv(lctx->NextRank(), "Recv ct of total row count");

  heu::lib::algorithms::elgamal::Ciphertext row_count_ct;
  row_count_ct.Deserialize(row_count_ct_buf);

  return row_count_ct;
}

uint64_t DkPirSender::ReceiveRowCount(
    std::shared_ptr<yacl::link::Context> lctx) {
  uint64_t row_count = 0;
  yacl::Buffer row_count_buf =
      lctx->Recv(lctx->NextRank(), "Recv total row count");
  YACL_ENFORCE(row_count_buf.size() == sizeof(uint64_t));
  std::memcpy(&row_count, row_count_buf.data(), sizeof(uint64_t));

  return row_count;
}

bool DkPirSender::CheckRowCount(
    const heu::lib::algorithms::elgamal::Ciphertext &row_count_ct,
    uint64_t row_count) {
  auto curve = public_key_.GetCurve();

  // poly_row_count = a * row_count + b * query_count_
  yacl::math::MPInt poly_row_count =
      ComputePoly(polynomial_, row_count, query_count_);

  yacl::crypto::EcPoint res =
      curve->MulDoubleBase(poly_row_count, secret_key_.GetX(), row_count_ct.c1);

  return curve->PointEqual(res, row_count_ct.c2);
}

void DkPirSender::SaveRowCount(uint64_t row_count) {
  std::ofstream fs(options_.result_file);
  fs << "count" << std::endl;
  fs << row_count << std::endl;
  fs.close();
}
}  // namespace psi::dkpir