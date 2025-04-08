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

#include "psi/algorithm/dkpir/test/receiver.h"

#include "absl/strings/str_split.h"

#include "psi/utils/csv_converter.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

namespace psi::dkpir::test {
Receiver::Receiver(size_t thread_count) {
  ::apsi::ThreadPoolMgr::SetThreadCount(thread_count);
}

void Receiver::LoadParams(const std::string& params_file) {
  params_ = psi::apsi_wrapper::BuildPsiParams(params_file);
}

std::vector<::apsi::Item> Receiver::ExtractItems(
    const std::string& query_file, const std::string& tmp_query_file,
    const std::string& key) {
  psi::ApsiCsvConverter receiver_query_converter(query_file, key);
  receiver_query_converter.ExtractQuery(tmp_query_file);

  std::unique_ptr<psi::apsi_wrapper::DBData> query_data;

  std::tie(query_data, orig_items_) =
      psi::apsi_wrapper::load_db_with_orig_items(tmp_query_file);

  auto& items = std::get<psi::apsi_wrapper::UnlabeledData>(*query_data);

  std::vector<::apsi::Item> items_vec(items.begin(), items.end());

  return items_vec;
}

std::string Receiver::RequestOPRF(const std::vector<::apsi::Item>& items) {
  oprf_receiver_ = std::make_shared<psi::dkpir::OPRFReceiver>(items);

  std::unique_ptr<::apsi::network::SenderOperation> sop =
      psi::dkpir::DkPirReceiver::CreateOPRFRequest(*oprf_receiver_);

  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.type = sop->type();
  std::stringstream ss;
  sop_header.save(ss);
  sop->save(ss);

  return ss.str();
}

std::string Receiver::RequestQuery(const std::string& oprf_response) {
  std::stringstream ss;
  ss << oprf_response;

  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  std::unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
      std::make_unique<::apsi::network::SenderOperationResponseOPRF>();
  sop_response->load(ss);

  ::apsi::OPRFResponse response = ::apsi::to_oprf_response(sop_response);

  std::vector<::apsi::HashedItem> oprf_items;

  std::tie(oprf_items, label_keys_) =
      psi::dkpir::DkPirReceiver::ExtractHashes(response, *oprf_receiver_);

  receiver_ = std::make_shared<psi::dkpir::DkPirReceiver>(*params_);

  auto query = receiver_->create_query(oprf_items);

  itt_ = std::move(query.second);

  std::stringstream ss_out;
  sop_header.type = query.first->type();
  sop_header.save(ss_out);
  query.first->save(ss_out);

  return ss_out.str();
}

std::vector<::apsi::receiver::MatchRecord> Receiver::ProcessQueryResponse(
    const std::string& query_response) {
  std::stringstream ss;
  ss << query_response;
  ::apsi::network::SenderOperationHeader sop_header;
  sop_header.load(ss);

  std::unique_ptr<::apsi::network::SenderOperationResponse> sop_response =
      std::make_unique<::apsi::network::SenderOperationResponseQuery>();
  sop_response->load(ss);

  ::apsi::QueryResponse response = ::apsi::to_query_response(sop_response);

  auto seal_context = receiver_->get_seal_context();

  std::vector<::apsi::ResultPart> rps;
  while (rps.size() < response->package_count) {
    std::unique_ptr<::apsi::network::ResultPackage> rp(
        std::make_unique<::apsi::network::ResultPackage>());
    rp->load(ss, seal_context);
    rps.emplace_back(std::move(rp));
  }

  return receiver_->process_result(label_keys_, itt_, rps);
}

heu::lib::algorithms::elgamal::Ciphertext Receiver::ComputeRowCountCt(
    CurveType curve_type,
    const std::vector<::apsi::receiver::MatchRecord>& intersection) {
  std::string curve_name = psi::dkpir::FetchCurveName(curve_type);
  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(curve_name);

  // To generate an evaluator object, a public_key is required as a constructor
  // argument, where only the curve is used, so it doesn't matter if the public
  // key is randomly generated
  yacl::math::MPInt x;
  yacl::math::MPInt::RandomLtN(curve->GetOrder(), &x);
  heu::lib::algorithms::elgamal::PublicKey public_key(curve, curve->MulBase(x));
  heu::lib::algorithms::elgamal::Evaluator evaluator(public_key);
  heu::lib::algorithms::elgamal::Ciphertext row_count_ct;

  bool has_found = false;

  // Compute the ciphertext of the total row count
  for (auto& mr : intersection) {
    if (mr.found) {
      auto byte_span = mr.label.get_as<uint8_t>();
      std::vector<uint8_t> byte_vector(byte_span.begin(), byte_span.end());
      yacl::Buffer ct_buf(byte_span.data(), byte_span.size());

      if (has_found) {
        heu::lib::algorithms::elgamal::Ciphertext tmp_ct;
        tmp_ct.Deserialize(ct_buf);
        evaluator.AddInplace(&row_count_ct, tmp_ct);
      } else {
        row_count_ct.Deserialize(ct_buf);
        has_found = true;
      }
    }
  }

  return row_count_ct;
}

uint64_t Receiver::ComputeRowCount(
    const std::vector<::apsi::receiver::MatchRecord>& intersection) {
  uint64_t row_count = 0;
  // Compute the total row count
  for (auto& mr : intersection) {
    if (mr.found) {
      std::string label = mr.label.to_string();
      std::vector<std::string> row_values = absl::StrSplit(label, "||");
      row_count += row_values.size();
    }
  }
  return row_count;
}

uint64_t Receiver::SaveResult(
    const std::vector<::apsi::receiver::MatchRecord>& intersection,
    const std::vector<::apsi::Item>& items, uint128_t shuffle_seed,
    uint64_t shuffle_counter, const std::string& result_file,
    const std::string& tmp_result_file, const std::string& key,
    const std::vector<std::string>& labels) {
  psi::dkpir::WriteIntersectionResults(orig_items_, items, intersection,
                                       shuffle_seed, shuffle_counter,
                                       tmp_result_file);

  psi::ApsiCsvConverter recevier_result_converter(tmp_result_file, "key",
                                                  {"value"});

  uint64_t row_count = ::seal::util::safe_cast<uint64_t>(
      recevier_result_converter.ExtractResult(result_file, key, labels));

  return row_count;
}
}  // namespace psi::dkpir::test