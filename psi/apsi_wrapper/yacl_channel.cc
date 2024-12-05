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

#include "psi/apsi_wrapper/yacl_channel.h"

// STD
#include <cstddef>
#include <stdexcept>
#include <utility>

// APSI
#include "apsi/log.h"

using namespace std;
using namespace seal;

namespace psi::apsi_wrapper {
void YaclChannel::send(unique_ptr<::apsi::network::SenderOperation> sop) {
  // Need to have the SenderOperation package
  if (!sop) {
    APSI_LOG_ERROR("Failed to send operation: operation data is missing");
    throw invalid_argument("operation data is missing");
  }

  // Construct the header
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.type = sop->type();
  APSI_LOG_DEBUG("Sending operation of type "
                 << sender_operation_type_str(sop_header.type));

  size_t old_bytes_sent = bytes_sent_;

  stringstream ss;

  bytes_sent_ += sop_header.save(ss);
  bytes_sent_ += sop->save(ss);

  lctx_->Send(lctx_->NextRank(), ss.str(), "sop");

  APSI_LOG_DEBUG("Sent an operation of type "
                 << sender_operation_type_str(sop_header.type) << " ("
                 << bytes_sent_ - old_bytes_sent << " bytes)");
}

unique_ptr<::apsi::network::SenderOperation> YaclChannel::receive_operation(
    shared_ptr<SEALContext> context,
    ::apsi::network::SenderOperationType expected) {
  bool valid_context = context && context->parameters_set();
  if (!valid_context &&
      (expected == ::apsi::network::SenderOperationType::sop_unknown ||
       expected == ::apsi::network::SenderOperationType::sop_query)) {
    // Cannot receive unknown or query operations without a valid SEALContext
    APSI_LOG_ERROR("Cannot receive an operation of type "
                   << sender_operation_type_str(expected)
                   << "; SEALContext is missing or invalid");
    return nullptr;
  }

  stringstream ss;

  ss << std::string_view(lctx_->Recv(lctx_->NextRank(), "sop"));

  size_t old_bytes_received = bytes_received_;

  ::apsi::network::SenderOperationHeader sop_header;
  try {
    bytes_received_ += sop_header.load(ss);
  } catch (const runtime_error &) {
    // Invalid header
    APSI_LOG_ERROR("Failed to receive a valid header");
    return nullptr;
  }

  if (!::apsi::same_serialization_version(sop_header.version)) {
    // Check that the serialization version numbers match
    APSI_LOG_ERROR(
        "Received header indicates a serialization version number ("
        << sop_header.version
        << ") incompatible with the current serialization version number ("
        << ::apsi::apsi_serialization_version << ")");
    return nullptr;
  }

  if (expected != ::apsi::network::SenderOperationType::sop_unknown &&
      expected != sop_header.type) {
    // Unexpected operation
    APSI_LOG_ERROR("Received header indicates an unexpected operation type "
                   << sender_operation_type_str(sop_header.type));
    return nullptr;
  }

  // Return value
  unique_ptr<::apsi::network::SenderOperation> sop = nullptr;

  try {
    switch (
        static_cast<::apsi::network::SenderOperationType>(sop_header.type)) {
      case ::apsi::network::SenderOperationType::sop_parms:
        sop = make_unique<::apsi::network::SenderOperationParms>();
        bytes_received_ += sop->load(ss);
        break;
      case ::apsi::network::SenderOperationType::sop_oprf:
        sop = make_unique<::apsi::network::SenderOperationOPRF>();
        bytes_received_ += sop->load(ss);
        break;
      case ::apsi::network::SenderOperationType::sop_query:
        sop = make_unique<::apsi::network::SenderOperationQuery>();
        bytes_received_ += sop->load(ss, std::move(context));
        break;
      default:
        // Invalid operation
        APSI_LOG_ERROR("Received header indicates an invalid operation type "
                       << sender_operation_type_str(sop_header.type));
        return nullptr;
    }
  } catch (const invalid_argument &ex) {
    APSI_LOG_ERROR(
        "An exception was thrown loading operation data: " << ex.what());
    return nullptr;
  } catch (const runtime_error &ex) {
    APSI_LOG_ERROR(
        "An exception was thrown loading operation data: " << ex.what());
    return nullptr;
  }

  // Loaded successfully
  APSI_LOG_DEBUG("Received an operation of type "
                 << sender_operation_type_str(sop_header.type) << " ("
                 << bytes_received_ - old_bytes_received << " bytes)");

  return sop;
}

void YaclChannel::send(
    unique_ptr<::apsi::network::SenderOperationResponse> sop_response) {
  // Need to have the SenderOperationResponse package
  if (!sop_response) {
    APSI_LOG_ERROR("Failed to send response: response data is missing");
    throw invalid_argument("response data is missing");
  }

  // Construct the header
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.type = sop_response->type();
  APSI_LOG_DEBUG("Sending response of type "
                 << sender_operation_type_str(sop_header.type));

  size_t old_bytes_sent = bytes_sent_;

  stringstream ss;

  bytes_sent_ += sop_header.save(ss);
  bytes_sent_ += sop_response->save(ss);

  lctx_->Send(lctx_->NextRank(), ss.str(), "sop_response");

  APSI_LOG_DEBUG("Sent a response of type "
                 << sender_operation_type_str(sop_header.type) << " ("
                 << bytes_sent_ - old_bytes_sent << " bytes)");
}

unique_ptr<::apsi::network::SenderOperationResponse>
YaclChannel::receive_response(::apsi::network::SenderOperationType expected) {
  stringstream ss;

  ss << std::string_view(lctx_->Recv(lctx_->NextRank(), "sop_response"));

  size_t old_bytes_received = bytes_received_;

  ::apsi::network::SenderOperationHeader sop_header;
  try {
    bytes_received_ += sop_header.load(ss);
  } catch (const runtime_error &) {
    // Invalid header
    APSI_LOG_ERROR("Failed to receive a valid header");
    return nullptr;
  }

  if (!::apsi::same_serialization_version(sop_header.version)) {
    // Check that the serialization version numbers match
    APSI_LOG_ERROR(
        "Received header indicates a serialization version number "
        << sop_header.version
        << " incompatible with the current serialization version number "
        << ::apsi::apsi_serialization_version);
    return nullptr;
  }

  if (expected != ::apsi::network::SenderOperationType::sop_unknown &&
      expected != sop_header.type) {
    // Unexpected operation
    APSI_LOG_ERROR("Received header indicates an unexpected operation type "
                   << sender_operation_type_str(sop_header.type));
    return nullptr;
  }

  // Return value
  unique_ptr<::apsi::network::SenderOperationResponse> sop_response = nullptr;

  try {
    switch (
        static_cast<::apsi::network::SenderOperationType>(sop_header.type)) {
      case ::apsi::network::SenderOperationType::sop_parms:
        sop_response =
            make_unique<::apsi::network::SenderOperationResponseParms>();
        bytes_received_ += sop_response->load(ss);
        break;
      case ::apsi::network::SenderOperationType::sop_oprf:
        sop_response =
            make_unique<::apsi::network::SenderOperationResponseOPRF>();
        bytes_received_ += sop_response->load(ss);
        break;
      case ::apsi::network::SenderOperationType::sop_query:
        sop_response =
            make_unique<::apsi::network::SenderOperationResponseQuery>();
        bytes_received_ += sop_response->load(ss);
        break;
      default:
        // Invalid operation
        APSI_LOG_ERROR("Received header indicates an invalid operation type "
                       << sender_operation_type_str(sop_header.type));
        return nullptr;
    }
  } catch (const runtime_error &ex) {
    APSI_LOG_ERROR(
        "An exception was thrown loading response data: " << ex.what());
    return nullptr;
  }

  // Loaded successfully
  APSI_LOG_DEBUG("Received a response of type "
                 << sender_operation_type_str(sop_header.type) << " ("
                 << bytes_received_ - old_bytes_received << " bytes)");

  return sop_response;
}

void YaclChannel::send(unique_ptr<::apsi::network::ResultPackage> rp) {
  // Need to have the ResultPackage
  if (!rp) {
    APSI_LOG_ERROR(
        "Failed to send result package: result package data is missing");
    throw invalid_argument("result package data is missing");
  }

  stringstream ss;

  bytes_sent_ += rp->save(ss);

  lctx_->Send(lctx_->NextRank(), ss.str(), "rp");
}

unique_ptr<::apsi::network::ResultPackage> YaclChannel::receive_result(
    shared_ptr<SEALContext> context) {
  bool valid_context = context && context->parameters_set();
  if (!valid_context) {
    // Cannot receive a result package without a valid SEALContext
    APSI_LOG_ERROR(
        "Cannot receive a result package; SEALContext is missing or invalid");
    return nullptr;
  }

  stringstream ss;

  ss << std::string_view(lctx_->Recv(lctx_->NextRank(), "rp"));

  size_t old_bytes_received = bytes_received_;

  // Return value
  unique_ptr<::apsi::network::ResultPackage> rp(
      make_unique<::apsi::network::ResultPackage>());

  try {
    bytes_received_ += rp->load(ss, std::move(context));
  } catch (const invalid_argument &ex) {
    APSI_LOG_ERROR(
        "An exception was thrown loading result package data: " << ex.what());
    return nullptr;
  } catch (const runtime_error &ex) {
    APSI_LOG_ERROR(
        "An exception was thrown loading result package data: " << ex.what());
    return nullptr;
  }

  // Loaded successfully
  APSI_LOG_DEBUG("Received a result package ("
                 << bytes_received_ - old_bytes_received << " bytes)");

  return rp;
}
}  // namespace psi::apsi_wrapper
