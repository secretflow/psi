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
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

// APSI
#include "apsi/network/sender_operation.h"
#include "apsi/network/zmq/zmq_channel.h"
#include "apsi/oprf/oprf_sender.h"
#include "apsi/sender_db.h"

#include "psi/apsi_wrapper/utils/bucket.h"
#include "psi/apsi_wrapper/utils/group_db.h"
#include "psi/apsi_wrapper/yacl_channel.h"

namespace psi::apsi_wrapper::cli {
/**
The SenderDispatcher is in charge of handling incoming requests through the
network.
*/
class SenderDispatcher {
 public:
  SenderDispatcher() = delete;

  /**
  Creates a new SenderDispatcher object. This constructor accepts both a
  SenderDB object, as well as a separately provided OPRF key. It uses the
  provided OPRF key to respond to OPRF requests, instead of attempting to
  retrieve a key from the SenderDB. This is necessary, for example, when the
  SenderDB is stripped, in which case it no longer carries a valid OPRF key.
  */
  SenderDispatcher(std::shared_ptr<::apsi::sender::SenderDB> sender_db,
                   ::apsi::oprf::OPRFKey oprf_key);

  /**
  Creates a new SenderDispatcher object. This constructor accepts a SenderDB
  object. It attempts to retrieve an OPRF key from the SenderDB and uses it to
  serve OPRF requests. This constructor cannot be used if the SenderDB is
  stripped, because the OPRF key is no longer available through the SenderDB.
  */
  SenderDispatcher(std::shared_ptr<::apsi::sender::SenderDB> sender_db);

  SenderDispatcher(std::shared_ptr<BucketSenderDbSwitcher> bucket_db_switcher);

  SenderDispatcher(GroupDB &group_db);

  /**
  Run the dispatcher on the given port.
  */
  void run(const std::atomic<bool> &stop, int port,
           bool streaming_result = true);

  void run(std::atomic<bool> &stop, std::shared_ptr<yacl::link::Context> lctx,
           bool streaming_result = true);

 private:
  GroupDB *group_db_ = nullptr;

  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;

  ::apsi::oprf::OPRFKey oprf_key_;

  std::shared_ptr<BucketSenderDbSwitcher> bucket_db_switcher_;

  void LoadBucket();

  void SetBucketIdx(size_t idx);

  /**
  Dispatch a Get Parameters request to the Sender.
  */
  void dispatch_parms(std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
                      ::apsi::network::ZMQSenderChannel &channel);

  void dispatch_parms(std::unique_ptr<::apsi::network::SenderOperation> sop,
                      YaclChannel &channel);

  /**
  Dispatch an OPRF query request to the Sender.
  */
  void dispatch_oprf(std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
                     ::apsi::network::ZMQSenderChannel &channel);

  void dispatch_oprf(std::unique_ptr<::apsi::network::SenderOperation> sop,
                     YaclChannel &channel, std::atomic<bool> &stop);

  /**
  Dispatch a Query request to the Sender.
  */
  void dispatch_query(std::unique_ptr<::apsi::network::ZMQSenderOperation> sop,
                      ::apsi::network::ZMQSenderChannel &channel,
                      bool streaming_result = true);

  void dispatch_query(std::unique_ptr<::apsi::network::SenderOperation> sop,
                      YaclChannel &channel, bool streaming_result = true);
};  // class SenderDispatcher
}  // namespace psi::apsi_wrapper::cli
