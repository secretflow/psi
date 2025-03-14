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

#include "psi/algorithm/dkpir/phe/phe.h"
#include "psi/algorithm/dkpir/sender.h"
#include "psi/wrapper/apsi/yacl_channel.h"

namespace psi::dkpir {
class DkPirSenderDispatcher {
 public:
  DkPirSenderDispatcher() = delete;

  DkPirSenderDispatcher(std::shared_ptr<::apsi::sender::SenderDB> sender_db,
                        std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db,
                        ::apsi::oprf::OPRFKey oprf_key,
                        const std::string &sk_file,
                        const std::string &result_file);

  void run(std::atomic<bool> &stop, std::shared_ptr<yacl::link::Context> lctx,
           bool streaming_result = true);

  void SendPublicKey(const std::shared_ptr<yacl::link::Context> &lctx) const;

  psi::dkpir::phe::Ciphertext ReceiveRowCountCt(
      const std::shared_ptr<yacl::link::Context> &lctx);

  void CheckRowCountAndSendShuffleSeed(
      const psi::dkpir::phe::Ciphertext &row_count_ct,
      const std::shared_ptr<yacl::link::Context> &lctx);

  void SaveResult(uint32_t row_count);

 private:
  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db_;

  // Public and private key pair of Elgamal encryption
  psi::dkpir::phe::PublicKey public_key_;
  psi::dkpir::phe::SecretKey secret_key_;

  ::apsi::oprf::OPRFKey oprf_key_;

  // A random linear function
  std::vector<uint32_t> polynomial_;

  // The seed that's used for shuffling the oprf results.
  uint64_t shuffle_seed_;

  // The count of items in the oprf query. Keep in mind, this isn't the final
  // row count, as the row count related to a certain item could exceed one.
  uint32_t query_count_;

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