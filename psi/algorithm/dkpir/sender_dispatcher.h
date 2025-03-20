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

#include <memory>

#include "apsi/oprf/oprf_sender.h"
#include "heu/library/algorithms/elgamal/elgamal.h"

#include "psi/algorithm/dkpir/sender.h"
#include "psi/wrapper/apsi/yacl_channel.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
class DkPirSenderDispatcher {
 public:
  DkPirSenderDispatcher() = delete;

  DkPirSenderDispatcher(std::shared_ptr<::apsi::sender::SenderDB> sender_db,
                        std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db,
                        ::apsi::oprf::OPRFKey oprf_key, CurveType curve_type,
                        const std::string &sk_file,
                        const std::string &result_file);

  void run(std::atomic<bool> &stop, std::shared_ptr<yacl::link::Context> lctx,
           bool streaming_result = true);

  heu::lib::algorithms::elgamal::Ciphertext ReceiveRowCountCt(
      const std::shared_ptr<yacl::link::Context> &lctx);

  void CheckRowCountAndSendShuffleSeed(
      const heu::lib::algorithms::elgamal::Ciphertext &row_count_ct,
      const std::shared_ptr<yacl::link::Context> &lctx);

  // Check if the ciphertext ct is the encrypted result of the plaintext m
  bool Check(const heu::lib::algorithms::elgamal::Ciphertext &ct,
             const yacl::math::MPInt &m);

  void SaveResult(uint64_t row_count);

 private:
  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db_;

  // Public and private key pair of Elgamal encryption
  heu::lib::algorithms::elgamal::PublicKey public_key_;
  heu::lib::algorithms::elgamal::SecretKey secret_key_;

  ::apsi::oprf::OPRFKey oprf_key_;

  // A random linear function
  std::vector<uint64_t> polynomial_;

  // The seed and counter that's used for shuffling the oprf results.
  uint128_t shuffle_seed_;
  uint64_t shuffle_counter_;

  // The count of items in the oprf query. Keep in mind, this isn't the final
  // row count, as the row count related to a certain item could exceed one.
  uint64_t query_count_;

  // Store the total row count
  std::string result_file_;

  void dispatch_parms(std::unique_ptr<::apsi::network::SenderOperation> sop,
                      psi::apsi_wrapper::YaclChannel &channel);

  void dispatch_oprf(std::unique_ptr<::apsi::network::SenderOperation> sop,
                     psi::apsi_wrapper::YaclChannel &channel,
                     std::atomic<bool> &stop);

  void dispatch_query(std::unique_ptr<::apsi::network::SenderOperation> sop,
                      psi::apsi_wrapper::YaclChannel &channel,
                      bool streaming_result = true);
};
}  // namespace psi::dkpir