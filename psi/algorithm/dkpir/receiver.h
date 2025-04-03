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

#pragma once

#include "psi/algorithm/dkpir/oprf_receiver.h"
#include "psi/wrapper/apsi/receiver.h"
#include "psi/wrapper/apsi/yacl_channel.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
class DkPirReceiver : public psi::apsi_wrapper::Receiver {
 public:
  DkPirReceiver(::apsi::PSIParams params)
      : psi::apsi_wrapper::Receiver(params) {}

  // Creates and returns a psi::dkpir::OPRFReceiver object for the given items.
  static OPRFReceiver CreateOPRFReceiver(
      const std::vector<::apsi::Item> &items);

  // Creates and returns a parameter request that can be sent to the sender with
  // the Receiver::SendRequest function.
  static ::apsi::Request CreateOPRFRequest(const OPRFReceiver &oprf_receiver,
                                           uint32_t bucket_idx = 0);
  // Extracts a vector of OPRF hashed items from an OPRFResponse and the
  // corresponding psi::dkpir::OPRFReceiver.
  static std::pair<std::vector<::apsi::HashedItem>,
                   std::vector<::apsi::LabelKey>>
  ExtractHashes(const ::apsi::OPRFResponse &oprf_response,
                const OPRFReceiver &oprf_receiver);

  // Performs an OPRF request on a vector of items through a given channel and
  // returns a vector of OPRF hashed items of the same size as the input vector.
  // The main modification is to replace apsi::oprf::OPRFReceiver with
  // psi::dkpir::OPRFReceiver.
  static std::pair<std::vector<::apsi::HashedItem>,
                   std::vector<::apsi::LabelKey>>
  RequestOPRF(const std::vector<::apsi::Item> &items,
              psi::apsi_wrapper::YaclChannel &chl, uint32_t bucket_idx = 0);

  // Receive a response to a query about data or row count, and put the
  // processing result into a vector.
  std::vector<::apsi::receiver::MatchRecord> ReceiveQuery(
      const std::vector<::apsi::LabelKey> &label_keys,
      const ::apsi::receiver::IndexTranslationTable &itt,
      psi::apsi_wrapper::YaclChannel &chl, bool streaming_result);

  // Performs a labeled PSI query, but initially receive the row count query
  // response from the sender. The data query response will only be received
  // after the row count ciphertext has been returned. The shuffle seed will
  // only be received once the row count verification has been successful.
  std::vector<::apsi::receiver::MatchRecord> RequestQuery(
      const std::vector<::apsi::HashedItem> &items,
      const std::vector<::apsi::LabelKey> &label_keys, uint128_t &shuffle_seed,
      uint64_t &shuffle_counter, psi::apsi_wrapper::YaclChannel &chl,
      CurveType curve_type, bool skip_count_check = false,
      bool streaming_result = true, uint32_t bucket_idx = 0);

  // Send the ciphertext of the total row count
  void SendRowCountCt(
      CurveType curve_type,
      const std::vector<::apsi::receiver::MatchRecord> &intersection,
      const std::shared_ptr<yacl::link::Context> &lctx);

  // Send the plaintext of the total row count
  void SendRowCount(
      const std::vector<::apsi::receiver::MatchRecord> &intersection,
      const std::shared_ptr<yacl::link::Context> &lctx);

  // Receive the shuffle seed used to restore the data
  void ReceiveShuffleSeed(const std::shared_ptr<yacl::link::Context> &lctx,
                          uint128_t &shuffle_seed, uint64_t &shuffle_counter);
};

}  // namespace psi::dkpir