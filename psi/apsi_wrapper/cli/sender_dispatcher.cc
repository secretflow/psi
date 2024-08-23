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

// STD
#include "psi/apsi_wrapper/cli/sender_dispatcher.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include "psi/apsi_wrapper/sender.h"
#include "psi/apsi_wrapper/utils/bucket.h"
#include "psi/apsi_wrapper/yacl_channel.h"

// APSI
#include "apsi/log.h"
#include "apsi/oprf/oprf_sender.h"
#include "apsi/requests.h"

// SEAL
#include "seal/util/common.h"

using namespace std::chrono_literals;

namespace psi::apsi_wrapper::cli {

SenderDispatcher::SenderDispatcher(
    std::shared_ptr<::apsi::sender::SenderDB> sender_db,
    ::apsi::oprf::OPRFKey oprf_key)
    : sender_db_(std::move(sender_db)), oprf_key_(std::move(oprf_key)) {
  if (!sender_db_) {
    throw std::invalid_argument("sender_db is not set");
  }

  // If SenderDB is not stripped, the OPRF key it holds must be equal to the
  // provided oprf_key
  if (!sender_db_->is_stripped() && oprf_key_ != sender_db->get_oprf_key()) {
    APSI_LOG_ERROR(
        "Failed to create SenderDispatcher: SenderDB OPRF key differs "
        "from the given OPRF key");
    throw std::logic_error("mismatching OPRF keys");
  }
}

SenderDispatcher::SenderDispatcher(
    std::shared_ptr<::apsi::sender::SenderDB> sender_db)
    : sender_db_(std::move(sender_db)) {
  if (!sender_db_) {
    throw std::invalid_argument("sender_db is not set");
  }

  try {
    oprf_key_ = sender_db_->get_oprf_key();
  } catch (const std::logic_error &) {
    APSI_LOG_ERROR("Failed to create SenderDispatcher: missing OPRF key");
    throw;
  }
}

SenderDispatcher::SenderDispatcher(
    std::shared_ptr<BucketSenderDbSwitcher> bucket_db_switcher)
    : bucket_db_switcher_(bucket_db_switcher) {
  LoadBucket();
}

SenderDispatcher::SenderDispatcher(GroupDB &group_db) : group_db_(&group_db) {
  auto bucket_num = group_db_->GetBucketNum();
  for (size_t i = 0; i != bucket_num; ++i) {
    SetBucketIdx(i);
    if (sender_db_ != nullptr) {
      break;
    }
  }

  YACL_ENFORCE(sender_db_ != nullptr,
               "Can not found a valid bucket, terminated.");
}

void SenderDispatcher::SetBucketIdx(size_t idx) {
  if (group_db_ != nullptr) {
    auto item = group_db_->GetBucketDB(idx);

    sender_db_ = item.sender_db;
    oprf_key_ = item.oprf_key;
    return;
  }

  if (!bucket_db_switcher_) {
    return;
  }

  if (idx == bucket_db_switcher_->bucket_idx()) {
    return;
  }

  bucket_db_switcher_->SetBucketIdx(idx);
  LoadBucket();
}

void SenderDispatcher::LoadBucket() {
  if (!bucket_db_switcher_) {
    return;
  }

  sender_db_ = bucket_db_switcher_->GetSenderDB();
  oprf_key_ = bucket_db_switcher_->GetOPRFKey();

  if (!sender_db_) {
    throw std::invalid_argument("sender_db is not set");
  }

  // If SenderDB is not stripped, the OPRF key it holds must be equal to the
  // provided oprf_key
  if (!sender_db_->is_stripped() && oprf_key_ != sender_db_->get_oprf_key()) {
    APSI_LOG_ERROR(
        "Failed to create SenderDispatcher: SenderDB OPRF key differs "
        "from the given OPRF key");
    throw std::logic_error("mismatching OPRF keys");
  }
}

void SenderDispatcher::run(const std::atomic<bool> &stop, int port,
                           bool streaming_result) {
  ::apsi::network::ZMQSenderChannel chl;

  std::stringstream ss;
  ss << "tcp://*:" << port;

  APSI_LOG_INFO("SenderDispatcher listening on port " << port);
  chl.bind(ss.str());

  auto seal_context = sender_db_->get_seal_context();

  // Run until stopped
  bool logged_waiting = false;
  while (!stop) {
    std::unique_ptr<::apsi::network::ZMQSenderOperation> sop;
    if (!(sop = chl.receive_network_operation(seal_context))) {
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

    switch (sop->sop->type()) {
      case ::apsi::network::SenderOperationType::sop_parms:
        APSI_LOG_INFO("Received parameter request");
        dispatch_parms(std::move(sop), chl);
        break;

      case ::apsi::network::SenderOperationType::sop_oprf:
        APSI_LOG_INFO("Received OPRF request");
        dispatch_oprf(std::move(sop), chl);
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

void SenderDispatcher::run(std::atomic<bool> &stop,
                           std::shared_ptr<yacl::link::Context> lctx,
                           bool streaming_result) {
  YaclChannel chl(lctx);

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

void SenderDispatcher::dispatch_parms(
    std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
    ::apsi::network::ZMQSenderChannel &chl) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_params");

  try {
    // Extract the parameter request
    ::apsi::ParamsRequest params_request =
        ::apsi::to_params_request(std::move(sop->sop));

    Sender::RunParams(
        params_request, sender_db_, chl,
        [&sop](::apsi::network::Channel &c,
               std::unique_ptr<::apsi::network::SenderOperationResponse>
                   sop_response) {
          auto nsop_response =
              std::make_unique<::apsi::network::ZMQSenderOperationResponse>();
          nsop_response->sop_response = std::move(sop_response);
          nsop_response->client_id = std::move(sop->client_id);

          // We know for sure that the channel is a SenderChannel so use
          // static_cast
          static_cast<::apsi::network::ZMQSenderChannel &>(c).send(
              std::move(nsop_response));
        });
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Sender threw an exception while processing parameter request: "
        << ex.what());
  }
}

void SenderDispatcher::dispatch_parms(
    std::unique_ptr<::apsi::network::SenderOperation> sop, YaclChannel &chl) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_params");

  try {
    // Extract the parameter request
    ::apsi::ParamsRequest params_request =
        ::apsi::to_params_request(std::move(sop));

    Sender::RunParams(params_request, sender_db_, chl);
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Sender threw an exception while processing parameter request: "
        << ex.what());
  }
}

void SenderDispatcher::dispatch_oprf(
    std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
    ::apsi::network::ZMQSenderChannel &chl) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_oprf");

  try {
    // Extract the OPRF request
    ::apsi::OPRFRequest oprf_request =
        ::apsi::to_oprf_request(std::move(sop->sop));

    SetBucketIdx(oprf_request->bucket_idx);

    Sender::RunOPRF(
        oprf_request, oprf_key_, chl,
        [&sop](::apsi::network::Channel &c,
               std::unique_ptr<::apsi::network::SenderOperationResponse>
                   sop_response) {
          auto nsop_response =
              std::make_unique<::apsi::network::ZMQSenderOperationResponse>();
          nsop_response->sop_response = std::move(sop_response);
          nsop_response->client_id = std::move(sop->client_id);

          // We know for sure that the channel is a SenderChannel so use
          // static_cast
          static_cast<::apsi::network::ZMQSenderChannel &>(c).send(
              std::move(nsop_response));
        });
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR("Sender threw an exception while processing OPRF request: "
                   << ex.what());
  }
}

void SenderDispatcher::dispatch_oprf(
    std::unique_ptr<::apsi::network::SenderOperation> sop, YaclChannel &chl,
    std::atomic<bool> &stop) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_oprf");

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

    SetBucketIdx(oprf_request->bucket_idx);

    Sender::RunOPRF(oprf_request, oprf_key_, chl);
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR("Sender threw an exception while processing OPRF request: "
                   << ex.what());
  }
}

void SenderDispatcher::dispatch_query(
    std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
    ::apsi::network::ZMQSenderChannel &chl, bool streaming_result) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_query");

  try {
    auto query_request = ::apsi::to_query_request(std::move(sop->sop));

    SetBucketIdx(query_request->bucket_idx);

    auto send_func = [&sop](::apsi::network::Channel &c,
                            ::apsi::Response response) {
      auto nsop_response =
          std::make_unique<::apsi::network::ZMQSenderOperationResponse>();
      nsop_response->sop_response = std::move(response);
      nsop_response->client_id = sop->client_id;

      // We know for sure that the channel is a SenderChannel so use
      // static_cast
      static_cast<::apsi::network::ZMQSenderChannel &>(c).send(
          std::move(nsop_response));
    };

    if (sender_db_ == nullptr) {
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

    // Create the Query object
    apsi::sender::Query query(std::move(query_request), sender_db_);

    // Query will send result to client in a stream of ResultPackages
    // (ResultParts)
    Sender::RunQuery(
        query, chl, streaming_result,
        // Lambda function for sending the query response
        send_func,
        // Lambda function for sending the result parts
        [&sop](::apsi::network::Channel &c, ::apsi::ResultPart rp) {
          auto nrp = std::make_unique<apsi::network::ZMQResultPackage>();
          nrp->rp = std::move(rp);
          nrp->client_id = sop->client_id;

          // We know for sure that the channel is a SenderChannel so use
          // static_cast
          static_cast<::apsi::network::ZMQSenderChannel &>(c).send(
              std::move(nrp));
        });
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Sender threw an exception while processing query: " << ex.what());
  }
}

void SenderDispatcher::dispatch_query(
    std::unique_ptr<::apsi::network::SenderOperation> sop, YaclChannel &chl,
    bool streaming_result) {
  STOPWATCH(sender_stopwatch, "SenderDispatcher::dispatch_query");

  try {
    // Create the Query object
    auto query_request = ::apsi::to_query_request(std::move(sop));

    SetBucketIdx(query_request->bucket_idx);

    auto send_func = Sender::BasicSend<::apsi::Response::element_type>;

    if (sender_db_ == nullptr) {
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

    // Create the Query object
    apsi::sender::Query query(std::move(query_request), sender_db_);

    // Query will send result to client in a stream of ResultPackages
    // (ResultParts)
    Sender::RunQuery(query, chl, streaming_result);
  } catch (const std::exception &ex) {
    APSI_LOG_ERROR(
        "Sender threw an exception while processing query: " << ex.what());
  }
}
}  // namespace psi::apsi_wrapper::cli
