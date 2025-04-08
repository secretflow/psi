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

#include "psi/algorithm/dkpir/test/sender.h"

#include "apsi/network/stream_channel.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/dkpir/common.h"
#include "psi/algorithm/dkpir/sender.h"
#include "psi/utils/csv_converter.h"

namespace psi::dkpir::test {

Sender::Sender(const DkPirSenderOptions &options, size_t thread_count) {
  ::apsi::ThreadPoolMgr::SetThreadCount(thread_count);
  psi::dkpir::SenderOffline(options);
  shuffle_seed_ = yacl::crypto::SecureRandSeed();
  shuffle_counter_ = yacl::crypto::SecureRandU64();
}

void Sender::LoadSenderDB(const std::string &value_sdb_file,
                          const std::string &count_sdb_file,
                          const std::string &params_file) {
  sender_db_ = psi::apsi_wrapper::TryLoadSenderDB(value_sdb_file, params_file,
                                                  oprf_key_);

  sender_cnt_db_ = psi::apsi_wrapper::TryLoadSenderDB(count_sdb_file,
                                                      params_file, oprf_key_);
}

void Sender::LoadSecretKey(CurveType curve_type, const std::string &sk_file) {
  yacl::math::MPInt x;
  polynomial_.resize(2);

  std::ifstream fs(sk_file, std::ios::binary);
  psi::dkpir::Load(polynomial_, x, fs);
  fs.close();

  std::string curve_name = FetchCurveName(curve_type);
  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(curve_name);

  secret_key_ = heu::lib::algorithms::elgamal::SecretKey(x, curve);
  public_key_ =
      heu::lib::algorithms::elgamal::PublicKey(curve, curve->MulBase(x));
}

std::string Sender::RunOPRF(const std::string &oprf_request_str) {
  std::stringstream ss;
  ss << oprf_request_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  std::unique_ptr<::apsi::network::SenderOperation> sop =
      std::make_unique<::apsi::network::SenderOperationOPRF>();
  sop->load(ss);

  ::apsi::OPRFRequest oprf_request = ::apsi::to_oprf_request(std::move(sop));

  query_count_ = oprf_request->data.size() / ::apsi::oprf::oprf_query_size;

  ::apsi::OPRFResponse response = psi::dkpir::DkPirSender::GenerateOPRFResponse(
      oprf_request, oprf_key_, shuffle_seed_, shuffle_counter_);

  sop_header.type = response->type();

  std::stringstream ss_out;

  sop_header.save(ss_out);
  response->save(ss_out);

  return ss_out.str();
}

std::string Sender::RunQuery(const std::string &query_str,
                             bool is_count_query) {
  std::stringstream ss;
  ss << query_str;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  std::unique_ptr<::apsi::network::SenderOperation> sop =
      std::make_unique<::apsi::network::SenderOperationQuery>();

  sop->load(ss, sender_db_->get_seal_context());

  auto query_request = ::apsi::to_query_request(std::move(sop));

  psi::dkpir::DkPirQuery query(std::move(query_request), sender_db_,
                               sender_cnt_db_);

  {
    // the following is copied from DkPirSender::RunSender
    // We use a custom SEAL memory that is freed after the query is done
    auto pool = ::seal::MemoryManager::GetPool(::seal::mm_force_new);

    ::apsi::ThreadPoolMgr tpm;

    // Determine whether to perform a count query based on is_count_query
    std::shared_ptr<apsi::sender::SenderDB> db;
    if (is_count_query) {
      db = query.sender_cnt_db();
    } else {
      db = query.sender_db();
    }

    // Acquire a reader lock
    auto db_lock = db->get_reader_lock();

    // Copy over the CryptoContext from ::apsi::sender::SenderDB; set the
    // Evaluator for this local instance. Relinearization keys may not have been
    // included in the query. In that case query.relin_keys() simply holds an
    // empty seal::RelinKeys instance. There is no problem with the below call
    // to CryptoContext::set_evaluator.
    ::apsi::CryptoContext crypto_context(db->get_crypto_context());
    crypto_context.set_evaluator(query.relin_keys());

    // Get the PSIParams
    ::apsi::PSIParams params(db->get_params());

    uint32_t bundle_idx_count = params.bundle_idx_count();
    uint32_t max_items_per_bin = params.table_params().max_items_per_bin;

    // Extract the PowersDag
    ::apsi::PowersDag pd = query.pd();

    // The query response only tells how many ResultPackages to expect; send
    // this first
    uint32_t package_count =
        ::seal::util::safe_cast<uint32_t>(db->get_bin_bundle_count());
    ::apsi::QueryResponse response_query =
        std::make_unique<::apsi::QueryResponse::element_type>();
    response_query->package_count = package_count;

    // For each bundle index i, we need a vector of powers of the query Qᵢ. We
    // need powers all the way up to Qᵢ^max_items_per_bin. We don't store the
    // zeroth power. If Paterson-Stockmeyer is used, then only a subset of the
    // powers will be populated.
    std::vector<psi::apsi_wrapper::CiphertextPowers> all_powers(
        bundle_idx_count);

    // Initialize powers
    for (psi::apsi_wrapper::CiphertextPowers &powers : all_powers) {
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
      psi::apsi_wrapper::Sender::ComputePowers(
          db, crypto_context, all_powers, pd, static_cast<uint32_t>(bundle_idx),
          pool);
    }

    std::vector<std::future<void>> futures;
    std::vector<::apsi::ResultPart> rps;
    std::mutex rps_mutex;

    std::stringstream ss_c;
    ::apsi::network::StreamChannel chl(ss_c);

    for (size_t bundle_idx = 0; bundle_idx < bundle_idx_count; bundle_idx++) {
      auto bundle_caches = db->get_cache_at(static_cast<uint32_t>(bundle_idx));
      for (auto &cache : bundle_caches) {
        futures.push_back(tpm.thread_pool().enqueue([&, bundle_idx, cache]() {
          ::psi::apsi_wrapper::Sender::ProcessBinBundleCache(
              db, crypto_context, cache, all_powers, chl,
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

    std::stringstream ss_out;
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

bool Sender::CheckRowCount(
    const heu::lib::algorithms::elgamal::Ciphertext &row_count_ct,
    const uint64_t &row_count) {
  auto curve = public_key_.GetCurve();

  // poly_row_count = a * row_count + b * query_count_
  yacl::math::MPInt poly_row_count =
      ComputePoly(polynomial_, row_count, query_count_);

  yacl::crypto::EcPoint res =
      curve->MulDoubleBase(poly_row_count, secret_key_.GetX(), row_count_ct.c1);

  return curve->PointEqual(res, row_count_ct.c2);
}
}  // namespace psi::dkpir::test