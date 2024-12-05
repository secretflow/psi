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

#pragma once

// STD
#include <iostream>
#include <memory>
#include <mutex>

// APSI
#include "apsi/network/network_channel.h"
#include "apsi/network/result_package.h"
#include "apsi/network/sender_operation.h"
#include "apsi/network/sender_operation_response.h"
#include "yacl/link/link.h"

namespace psi::apsi_wrapper {
class YaclChannel : public ::apsi::network::NetworkChannel {
 public:
  YaclChannel() = delete;

  explicit YaclChannel(std::shared_ptr<yacl::link::Context> lctx)
      : lctx_(lctx) {}

  ~YaclChannel() {}

  /**
  Send a SenderOperation from a receiver to a sender. These operations represent
  either a parameter request, an OPRF request, or a query request. The function
  throws an exception on failure.
  */
  void send(std::unique_ptr<::apsi::network::SenderOperation> sop) override;

  /**
  Receive a SenderOperation from a receiver. Operations of type sop_query and
  sop_unknown require a valid seal::SEALContext to be provided. For operations
  of type sop_parms and sop_oprf the context can be set as nullptr. The function
  returns nullptr on failure.
  */
  std::unique_ptr<::apsi::network::SenderOperation> receive_operation(
      std::shared_ptr<seal::SEALContext> context,
      ::apsi::network::SenderOperationType expected =
          ::apsi::network::SenderOperationType::sop_unknown) override;

  /**
  Send a SenderOperationResponse from a sender to a receiver. These operations
  represent a response to either a parameter request, an OPRF request, or a
  query request. The function throws and exception on failure.
  */
  void send(std::unique_ptr<::apsi::network::SenderOperationResponse>
                sop_response) override;

  /**
  Receive a SenderOperationResponse from a sender. The function returns nullptr
  on failure.
  */
  std::unique_ptr<::apsi::network::SenderOperationResponse> receive_response(
      ::apsi::network::SenderOperationType expected =
          ::apsi::network::SenderOperationType::sop_unknown) override;

  /**
  Send a ResultPackage to a receiver. The function throws and exception on
  failure.
  */
  void send(std::unique_ptr<::apsi::network::ResultPackage> rp) override;

  /**
  Receive a ResultPackage from a sender. A valid seal::SEALContext must be
  provided. The function returns nullptr on failure.
  */
  std::unique_ptr<::apsi::network::ResultPackage> receive_result(
      std::shared_ptr<seal::SEALContext> context) override;

 protected:
  std::shared_ptr<yacl::link::Context> lctx_;
};  // class StreamChannel
}  // namespace psi::apsi_wrapper
