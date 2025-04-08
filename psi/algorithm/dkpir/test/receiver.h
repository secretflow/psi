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

#include "heu/library/algorithms/elgamal/elgamal.h"

#include "psi/algorithm/dkpir/common.h"
#include "psi/algorithm/dkpir/oprf_receiver.h"
#include "psi/algorithm/dkpir/receiver.h"
#include "psi/utils/random_str.h"

namespace psi::dkpir::test {
// This is a class for receiver where communication has been removed, retaining
// only the core logic of the dkpir algorithm for unit testing.
class Receiver {
 public:
  Receiver(size_t thread_count = std::thread::hardware_concurrency());

  void LoadParams(const std::string& params_file);

  std::vector<::apsi::Item> ExtractItems(const std::string& query_file,
                                         const std::string& tmp_query_file,
                                         const std::string& key);

  std::string RequestOPRF(const std::vector<::apsi::Item>& items);

  std::string RequestQuery(const std::string& oprf_response);

  std::vector<::apsi::receiver::MatchRecord> ProcessQueryResponse(
      const std::string& query_response);

  heu::lib::algorithms::elgamal::Ciphertext ComputeRowCountCt(
      CurveType curve_type,
      const std::vector<::apsi::receiver::MatchRecord>& intersection);

  uint64_t ComputeRowCount(
      const std::vector<::apsi::receiver::MatchRecord>& intersection);

  uint64_t SaveResult(
      const std::vector<::apsi::receiver::MatchRecord>& intersection,
      const std::vector<::apsi::Item>& items, uint128_t shuffle_seed,
      uint64_t shuffle_counter, const std::string& result_file,
      const std::string& tmp_result_file, const std::string& key,
      const std::vector<std::string>& labels);

 private:
  std::unique_ptr<::apsi::PSIParams> params_;

  std::vector<std::string> orig_items_;

  std::vector<::apsi::LabelKey> label_keys_;

  ::apsi::receiver::IndexTranslationTable itt_;

  std::shared_ptr<psi::dkpir::OPRFReceiver> oprf_receiver_;
  std::shared_ptr<psi::dkpir::DkPirReceiver> receiver_;
};
}  // namespace psi::dkpir::test