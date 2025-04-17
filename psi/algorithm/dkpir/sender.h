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

#include <string>

#include "heu/library/algorithms/elgamal/elgamal.h"

#include "psi/algorithm/dkpir/query.h"
#include "psi/wrapper/apsi/sender.h"
#include "psi/wrapper/apsi/utils/common.h"
#include "psi/wrapper/apsi/yacl_channel.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
struct DkPirSenderOptions {
  std::size_t threads = 1;
  std::size_t nonce_byte_count = 16;
  bool compress = false;
  bool streaming_result = true;
  bool skip_count_check = false;
  CurveType curve_type;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::string params_file;
  std::string source_file;
  std::string value_sdb_out_file;
  std::string count_sdb_out_file;
  std::string secret_key_file;
  std::string result_file;

  std::string tmp_folder;

  std::string key;
  std::vector<std::string> labels;
};

class DkPirSender {
 public:
  DkPirSender() = default;

  DkPirSender(const DkPirSenderOptions &options);

  // Offline phase. Step 1: data preprocessing, which mainly includes merging
  // labels of the same key. If the row count check is not skipped, a table will
  // also be generated to record the number of duplicate rows of the key.
  void PreProcessData(const std::string &key_value_file,
                      const std::string &key_count_file);

  // Offline phase. Step 2: generate DB, which creates sender_db based on the
  // data table. If the row count check is not skipped, then a sender_cnt_db
  // will be generated based on the row count table. Both sender_db and
  // sender_cnt_db are smart pointers of SenderDB, they just store different
  // labels, and we use naming to distinguish them.
  void GenerateDB(const std::string &key_value_file,
                  const std::string &key_count_file);

  // The following are the methods used in the online stage. This method is used
  // to load the local sender_db and sender_cnt_db.
  void LoadDB();

  // Load the random linear function and the private key required for Elgamal
  // encryption.
  void LoadSecretKey();

  // Wait for a valid OPRF request or Query request from Receiver.
  ::apsi::Request ReceiveRequest(psi::apsi_wrapper::YaclChannel &chl);

  // Handle OPRF request, if skipping row count check, then directly call
  // psi::apsi_wrapper::Sender::GenerateOPRFResponse method to generate OPRF
  // response. Otherwise, shuffle the OPRF results.
  ::apsi::OPRFResponse RunOPRF(::apsi::Request request);

  // Generate and send a response to a query. Compared to the method
  // in the psi::apsi_wrapper::Sender, the main modification is to to determine
  // whether to handle queries for row count based on is_count_query
  void RunQuery(
      const psi::dkpir::DkPirQuery &query, psi::apsi_wrapper::YaclChannel &chl,
      bool is_count_query,
      std::function<void(::apsi::network::Channel &, ::apsi::Response)>
          send_fun = psi::apsi_wrapper::Sender::BasicSend<
              ::apsi::Response::element_type>,
      std::function<void(::apsi::network::Channel &, ::apsi::ResultPart)>
          send_rp_fun = psi::apsi_wrapper::Sender::BasicSend<
              ::apsi::ResultPart::element_type>);

  // Receive the ciphertext of the row count from Receiver
  heu::lib::algorithms::elgamal::Ciphertext ReceiveRowCountCt(
      std::shared_ptr<yacl::link::Context> lctx);

  //  Receive the row count from Receiver
  uint64_t ReceiveRowCount(std::shared_ptr<yacl::link::Context> lctx);

  // Verify whether the row count provided by the receiver are authentic
  bool CheckRowCount(
      const heu::lib::algorithms::elgamal::Ciphertext &row_count_ct,
      uint64_t row_count);

  // Store the total number of rows of the receiver's query
  void SaveRowCount(uint64_t row_count);

  uint128_t GetShuffleSeed() const { return shuffle_seed_; }
  uint64_t GetShuffleCounter() const { return shuffle_counter_; }

  std::shared_ptr<::apsi::sender::SenderDB> GetSenderDB() const {
    return sender_db_;
  }
  std::shared_ptr<::apsi::sender::SenderDB> GetSenderCntDB() const {
    return sender_cnt_db_;
  }

 private:
  DkPirSenderOptions options_;

  std::shared_ptr<::apsi::sender::SenderDB> sender_db_;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db_;

  ::apsi::oprf::OPRFKey oprf_key_;

  // Public and private key pair of Elgamal encryption
  heu::lib::algorithms::elgamal::PublicKey public_key_;
  heu::lib::algorithms::elgamal::SecretKey secret_key_;

  // A random linear function
  std::vector<uint64_t> polynomial_;

  // The seed and counter that's used for shuffling the oprf results
  uint128_t shuffle_seed_;
  uint64_t shuffle_counter_;

  // The count of items in the oprf query
  uint64_t query_count_;
};
}  // namespace psi::dkpir