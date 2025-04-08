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

#include "heu/library/algorithms/elgamal/elgamal.h"

#include "psi/algorithm/dkpir/entry.h"

namespace psi::dkpir::test {
// This is a class for sender where communication has been removed, retaining
// only the core logic of the dkpir algorithm for unit testing.
class Sender {
 public:
  Sender(const DkPirSenderOptions &options,
         size_t thread_count = std::thread::hardware_concurrency());

  void LoadSenderDB(const std::string &value_sdb_file,
                    const std::string &count_sdb_file,
                    const std::string &params_file);

  void LoadSecretKey(CurveType curve_type, const std::string &sk_file);

  std::string RunOPRF(const std::string &oprf_request_str);

  std::string RunQuery(const std::string &query_str, bool is_count_query);

  bool CheckRowCount(
      const heu::lib::algorithms::elgamal::Ciphertext &row_count_ct,
      const uint64_t &row_count);

  uint128_t GetShuffleSeed() const { return shuffle_seed_; }
  uint64_t GetShuffleCounter() const { return shuffle_counter_; }

 private:
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

}  // namespace psi::dkpir::test