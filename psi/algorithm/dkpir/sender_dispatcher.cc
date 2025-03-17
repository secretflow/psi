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

#include "psi/algorithm/dkpir/sender_dispatcher.h"

#include <fstream>

#include "apsi/requests.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/dkpir/common.h"
#include "psi/algorithm/dkpir/phe/phe.h"
#include "psi/algorithm/dkpir/query.h"

using namespace std::chrono_literals;

namespace psi::dkpir {
DkPirSenderDispatcher::DkPirSenderDispatcher(
    std::shared_ptr<::apsi::sender::SenderDB> sender_db,
    std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db,
    ::apsi::oprf::OPRFKey oprf_key, const std::string &sk_file,
    const std::string &result_file)
    : sender_db_(std::move(sender_db)),
      sender_cnt_db_(std::move(sender_cnt_db)),
      oprf_key_(std::move(oprf_key)),
      shuffle_seed_(yacl::crypto::SecureRandSeed()),
      shuffle_counter_(yacl::crypto::SecureRandU64()),
      query_count_(0),
      result_file_(result_file) {
  if (!sender_db_ || !sender_cnt_db_) {
    YACL_THROW("sender_db or sender_cnt_db is not set");
  }

  // If SenderDB is not stripped, the OPRF key it holds must be equal to the
  // provided oprf_key
  if (!sender_db_->is_stripped() && oprf_key_ != sender_db_->get_oprf_key()) {
    SPDLOG_ERROR(
        "Failed to create DkPirSenderDispatcher: SenderDB OPRF key differs "
        "from the given OPRF key");
    YACL_THROW("mismatching OPRF keys");
  }

  // Load the random polynomial and the private key
  yacl::math::MPInt x;
  polynomial_.resize(2);

  std::ifstream fs(sk_file, std::ios::binary);
  psi::dkpir::Load(polynomial_, x, fs);
  fs.close();

  SPDLOG_INFO("Loaded the random polynomial and the private key from {}",
              sk_file);

  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(
          "fourq", yacl::ArgLib = "FourQlib");

  psi::dkpir::phe::KeyGenerator::GenerateKey(curve, x, &public_key_,
                                             &secret_key_);
}

void DkPirSenderDispatcher::run(std::atomic<bool> &stop,
                                std::shared_ptr<yacl::link::Context> lctx,
                                bool streaming_result) {
  psi::apsi_wrapper::YaclChannel chl(lctx);

  auto seal_context = sender_db_->get_seal_context();

  bool logged_waiting = false;
  while (!stop) {
    std::unique_ptr<::apsi::network::SenderOperation> sop;
    if (!(sop = chl.receive_operation(seal_context))) {
      if (!logged_waiting) {
        // We want to log 'Waiting' only once, even if we have to wait
        // for several sleeps. And only once after processing a request as
        // well.
        logged_waiting = true;
        APSI_LOG_INFO("Waiting for request from Receiver");
      }

      std::this_thread::sleep_for(50ms);
      continue;
    }

    switch (sop->type()) {
      case ::apsi::network::SenderOperationType::sop_parms:
        APSI_LOG_INFO("Received parameter request");
        dispatch_parms(std::move(sop), chl);
        break;

      case ::apsi::network::SenderOperationType::sop_oprf:
        APSI_LOG_INFO("Received OPRF request");
        dispatch_oprf(std::move(sop), chl, stop);
        break;

      case ::apsi::network::SenderOperationType::sop_query:
        APSI_LOG_INFO("Received query");
        dispatch_query(std::move(sop), chl, streaming_result);
        break;

      default:
        // We should never reach this point
        throw std::runtime_error("invalid operation");
    }

    logged_waiting = false;
  }
}

void DkPirSenderDispatcher::SendPublicKey(
    const std::shared_ptr<yacl::link::Context> &lctx) const {
  auto curve = public_key_.GetCurve();
  auto pk_point = public_key_.GetPk();

  yacl::Buffer pk_buf = curve->SerializePoint(pk_point);
  lctx->SendAsync(lctx->NextRank(), pk_buf, "Send phe pk");

  SPDLOG_INFO("Sent the public key of phe");
}

psi::dkpir::phe::Ciphertext DkPirSenderDispatcher::ReceiveRowCountCt(
    const std::shared_ptr<yacl::link::Context> &lctx) {
  auto curve = public_key_.GetCurve();
  uint64_t ciphertext_size = curve->GetSerializeLength() * 2;

  yacl::Buffer row_count_ct_buf =
      lctx->Recv(lctx->NextRank(), "Recv ct of total row count");
  YACL_ENFORCE(static_cast<uint64_t>(row_count_ct_buf.size()) ==
               ciphertext_size);

  std::vector<uint8_t> row_count_ct_vec(ciphertext_size);
  std::memcpy(row_count_ct_vec.data(), row_count_ct_buf.data(),
              ciphertext_size);

  psi::dkpir::phe::Ciphertext row_count_ct;
  row_count_ct.DeserializeCiphertext(curve, row_count_ct_vec.data(),
                                     ciphertext_size);

  SPDLOG_INFO("Received the ciphertext of the total row count");

  return row_count_ct;
}

void DkPirSenderDispatcher::CheckRowCountAndSendShuffleSeed(
    const psi::dkpir::phe::Ciphertext &row_count_ct,
    const std::shared_ptr<yacl::link::Context> &lctx) {
  auto curve = public_key_.GetCurve();

  uint64_t row_count = 0;
  yacl::Buffer row_count_buf =
      lctx->Recv(lctx->NextRank(), "Recv total row count");
  YACL_ENFORCE(row_count_buf.size() == sizeof(uint64_t));
  std::memcpy(&row_count, row_count_buf.data(), sizeof(uint64_t));

  // poly_row_count = a * row_count + b * query_count_
  yacl::math::MPInt poly_row_count =
      ComputePoly(polynomial_, row_count, query_count_);

  YACL_ENFORCE(psi::dkpir::phe::Evaluator::Check(row_count_ct, poly_row_count,
                                                 secret_key_),
               "Check row count failed");

  SaveResult(row_count);
  SPDLOG_INFO(
      "The verification of the total row count was successful, the total row "
      "count was {}, and the result was stored in {}",
      row_count, result_file_);

  lctx->SendAsync(lctx->NextRank(),
                  yacl::ByteContainerView(&shuffle_seed_, sizeof(uint128_t)),
                  "Send shuffle seed");
  lctx->SendAsync(lctx->NextRank(),
                  yacl::ByteContainerView(&shuffle_counter_, sizeof(uint64_t)),
                  "Send shuffle counter");
  SPDLOG_INFO("Sent the shuffle seed and counter");
}

void DkPirSenderDispatcher::SaveResult(uint64_t row_count) {
  std::ofstream fs(result_file_);
  fs << "count" << std::endl;
  fs << row_count;
}

void DkPirSenderDispatcher::dispatch_parms(
    std::unique_ptr<::apsi::network::SenderOperation> sop,
    psi::apsi_wrapper::YaclChannel &chl) {
  STOPWATCH(sender_stopwatch, "DkPirSenderDispatcher::dispatch_params");

  try {
    // Extract the parameter request
    ::apsi::ParamsRequest params_request =
        ::apsi::to_params_request(std::move(sop));

    DkPirSender::RunParams(params_request, sender_db_, chl);
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Sender threw an exception while processing parameter request: "
        << ex.what());
  }
}

void DkPirSenderDispatcher::dispatch_oprf(
    std::unique_ptr<::apsi::network::SenderOperation> sop,
    psi::apsi_wrapper::YaclChannel &chl, std::atomic<bool> &stop) {
  STOPWATCH(sender_stopwatch, "DkPirSenderDispatcher::dispatch_oprf");

  try {
    // Extract the OPRF request
    ::apsi::OPRFRequest oprf_request = ::apsi::to_oprf_request(std::move(sop));

    // NOTE(junfeng): This is a hack, empty request with max bucket_idx is a
    // signal of stop.
    if (oprf_request->data.empty() &&
        oprf_request->bucket_idx == std::numeric_limits<uint32_t>::max()) {
      stop = true;
      return;
    }

    query_count_ = oprf_request->data.size() / ::apsi::oprf::oprf_query_size;
    SPDLOG_INFO("Receiver queried a total of {} keys", query_count_);

    DkPirSender::RunOPRF(oprf_request, oprf_key_, shuffle_seed_,
                         shuffle_counter_, chl);
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR("Sender threw an exception while processing OPRF request: "
                   << ex.what());
  }
}

void DkPirSenderDispatcher::dispatch_query(
    std::unique_ptr<::apsi::network::SenderOperation> sop,
    psi::apsi_wrapper::YaclChannel &chl, bool streaming_result) {
  STOPWATCH(sender_stopwatch, "DkPirSenderDispatcher::dispatch_query");

  try {
    // Create the QueryRequest
    auto query_request = ::apsi::to_query_request(std::move(sop));

    auto send_func = DkPirSender::BasicSend<::apsi::Response::element_type>;

    if (sender_db_ == nullptr || sender_cnt_db_ == nullptr) {
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

    // Create the DkPirQuery object which includes two databases
    DkPirQuery query(std::move(query_request), sender_db_, sender_cnt_db_);

    // Sender processes the row count query
    DkPirSender::RunQuery(query, chl, streaming_result);

    // Sender transmits the public key of phe
    SendPublicKey(chl.get_lctx());

    // Sender gets the encrypted sum from the receiver
    psi::dkpir::phe::Ciphertext row_count_ct =
        ReceiveRowCountCt(chl.get_lctx());

    // Sender processes the data query
    psi::apsi_wrapper::Sender::RunQuery(query, chl, streaming_result);

    // Sender checks the total row count and transmits the shuffle seed
    CheckRowCountAndSendShuffleSeed(row_count_ct, chl.get_lctx());
  } catch (const std::exception &ex) {
    SPDLOG_ERROR("Sender threw an exception while processing query: {}",
                 ex.what());
  }
}

}  // namespace psi::dkpir