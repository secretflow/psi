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

#include "psi/algorithm/dkpir/oprf_receiver.h"
#include "psi/wrapper/apsi/receiver.h"
#include "psi/wrapper/apsi/yacl_channel.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
struct DkPirReceiverOptions {
  std::size_t threads = 1;
  bool streaming_result = true;
  bool skip_count_check = false;
  CurveType curve_type;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::string params_file;
  std::string query_file;
  std::string result_file;

  std::string tmp_folder;

  std::string key;
  std::vector<std::string> labels;
};

class DkPirReceiver : public psi::apsi_wrapper::Receiver {
 public:
  DkPirReceiver(::apsi::PSIParams params, const DkPirReceiverOptions& options)
      : psi::apsi_wrapper::Receiver(params), options_(options) {}

  // Extract the query from the converted query file.
  std::vector<::apsi::Item> ExtractItems(const std::string& tmp_query_file);

  // Create and return a psi::dkpir::ShuffleOPRFReceiver object for the given
  // items.
  ShuffledOPRFReceiver CreateShuffledOPRFReceiver(
      const std::vector<::apsi::Item>& items);

  // Create and return a oprf request that can be sent to Sender with the
  // YaclChannel::send function.
  ::apsi::Request CreateOPRFRequest(const ShuffledOPRFReceiver& oprf_receiver,
                                    uint32_t bucket_idx = 0);

  // Receive OPRF response from Sender.
  ::apsi::OPRFResponse ReceiveOPRFResponse(psi::apsi_wrapper::YaclChannel& chl);

  // Extract a vector of OPRF hashed items and a vector of LabelKey from an
  // OPRFResponse. Wherein, OPRF hashed items are used to generate queries, and
  // LabelKey is used to process query responses.
  std::pair<std::vector<::apsi::HashedItem>, std::vector<::apsi::LabelKey>>
  ExtractHashes(const ::apsi::OPRFResponse& oprf_response,
                const ShuffledOPRFReceiver& oprf_receiver);

  // Create and return a query request based on oprf_items.
  ::apsi::Request CreateQueryRequest(
      const std::vector<::apsi::HashedItem>& oprf_items,
      uint32_t bucket_idx = 0);

  // Receive a response to a query about data or row count, and put the
  // processing result into a vector.
  std::vector<::apsi::receiver::MatchRecord> ReceiveQueryResponse(
      const std::vector<::apsi::LabelKey>& label_keys,
      psi::apsi_wrapper::YaclChannel& chl);

  // Compute the ciphertext of the total row count (Enc(p(count)))
  heu::lib::algorithms::elgamal::Ciphertext ComputeRowCountCt(
      const std::vector<::apsi::receiver::MatchRecord>& intersection);

  // Compute the plaintext of the total row count. The number of rows
  // corresponding to each key can be determined by the number of kRowDelimiter
  // separators in the label.
  uint64_t ComputeRowCount(
      const std::vector<::apsi::receiver::MatchRecord>& intersection);

  // Receive the shuffle seed used to restore the data
  void ReceiveShuffleSeed(std::shared_ptr<yacl::link::Context> lctx,
                          uint128_t& shuffle_seed, uint64_t& shuffle_counter);

  // Restore the correspondence between key and label based on the shuffle seed,
  // store the query result in tmp_result_file, and then decompose the labels
  // through the converter to generate the final result file.
  uint64_t SaveResult(
      const std::vector<::apsi::receiver::MatchRecord>& intersection,
      const std::vector<::apsi::Item>& items, uint128_t shuffle_seed,
      uint64_t shuffle_counter, const std::string& tmp_result_file);

 private:
  DkPirReceiverOptions options_;
  // The original query stored as std::string will be used to generate the final
  // result csv file.
  std::vector<std::string> orig_items_;

  // Translates a cuckoo table index to an index of the vector of items that
  // were used to create query.
  ::apsi::receiver::IndexTranslationTable itt_;
};
}  // namespace psi::dkpir