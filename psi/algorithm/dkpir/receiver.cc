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

#include "psi/algorithm/dkpir/receiver.h"

#include <thread>

#include "absl/strings/str_split.h"

#include "psi/algorithm/dkpir/common.h"
#include "psi/utils/csv_converter.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

using namespace std::chrono_literals;

namespace psi::dkpir {
std::vector<::apsi::Item> DkPirReceiver::ExtractItems(
    const std::string& tmp_query_file) {
  // Extract the original query and generate a temporary query file that meets
  // APSI requirements
  psi::ApsiCsvConverter receiver_query_converter(options_.query_file,
                                                 options_.key);
  receiver_query_converter.ExtractQueryTo(tmp_query_file);

  std::unique_ptr<psi::apsi_wrapper::DBData> query_data;

  std::tie(query_data, orig_items_) =
      psi::apsi_wrapper::load_db_with_orig_items(tmp_query_file);

  if (!query_data ||
      !std::holds_alternative<psi::apsi_wrapper::UnlabeledData>(*query_data)) {
    // Failed to read query file
    YACL_THROW("Failed to read query file {}", tmp_query_file);
  }

  auto& items = std::get<psi::apsi_wrapper::UnlabeledData>(*query_data);

  std::vector<::apsi::Item> items_vec(items.begin(), items.end());

  return items_vec;
}

ShuffledOPRFReceiver DkPirReceiver::CreateShuffledOPRFReceiver(
    const std::vector<::apsi::Item>& items) {
  STOPWATCH(::apsi::util::recv_stopwatch,
            "DkPirReceiver::CreateShuffledOPRFReceiver");

  ShuffledOPRFReceiver shuffled_oprf_receiver(items);
  APSI_LOG_INFO("Created ShuffledOPRFReceiver for "
                << shuffled_oprf_receiver.item_count() << " items");

  return shuffled_oprf_receiver;
}

::apsi::Request DkPirReceiver::CreateOPRFRequest(
    const ShuffledOPRFReceiver& oprf_receiver, uint32_t bucket_idx) {
  auto sop = std::make_unique<::apsi::network::SenderOperationOPRF>();
  sop->data = oprf_receiver.query_data();
  sop->bucket_idx = bucket_idx;
  APSI_LOG_INFO("Created OPRF request for " << oprf_receiver.item_count()
                                            << " items");

  return sop;
}

::apsi::OPRFResponse DkPirReceiver::ReceiveOPRFResponse(
    psi::apsi_wrapper::YaclChannel& chl) {
  // Wait for a valid message of the right type
  ::apsi::OPRFResponse response;
  bool logged_waiting = false;
  uint32_t retry_times = 0;

  while (!(response = ::apsi::to_oprf_response(chl.receive_response()))) {
    if (!logged_waiting) {
      // We want to log 'Waiting' only once, even if we have to wait for several
      // sleeps.
      logged_waiting = true;
      APSI_LOG_INFO("Waiting for response to OPRF request");
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(kRecvRetryIntervalMs));

    if (++retry_times >= kRecvRetryTimes) {
      YACL_THROW("Sender failed to receive request from Receiver");
    }
  }

  return response;
}

std::pair<std::vector<::apsi::HashedItem>, std::vector<::apsi::LabelKey>>
DkPirReceiver::ExtractHashes(const ::apsi::OPRFResponse& oprf_response,
                             const ShuffledOPRFReceiver& oprf_receiver) {
  STOPWATCH(::apsi::util::recv_stopwatch, "DkPirReceiver::ExtractHashes");

  if (!oprf_response) {
    YACL_THROW(
        "Failed to extract OPRF hashes for items: oprf_response is null");
  }

  auto response_size = oprf_response->data.size();
  uint64_t oprf_response_item_count =
      response_size / ::apsi::oprf::oprf_response_size;
  if ((response_size % ::apsi::oprf::oprf_response_size) != 0 ||
      (oprf_response_item_count != oprf_receiver.item_count())) {
    APSI_LOG_ERROR(
        "Failed to extract OPRF hashes for items: unexpected OPRF response "
        "size ("
        << response_size << " B)");
    YACL_THROW("Failed to extract OPRF hashes for items");
  }

  std::vector<::apsi::HashedItem> items(oprf_receiver.item_count());
  std::vector<::apsi::LabelKey> label_keys(oprf_receiver.item_count());
  oprf_receiver.process_responses(oprf_response->data, items, label_keys);
  APSI_LOG_INFO("Extracted OPRF hashes for " << oprf_response_item_count
                                             << " items");

  return make_pair(std::move(items), std::move(label_keys));
}

::apsi::Request DkPirReceiver::CreateQueryRequest(
    const std::vector<::apsi::HashedItem>& oprf_items, uint32_t bucket_idx) {
  auto query = create_query(oprf_items, bucket_idx);
  itt_ = std::move(query.second);

  return std::move(query.first);
}

std::vector<::apsi::receiver::MatchRecord> DkPirReceiver::ReceiveQueryResponse(
    const std::vector<::apsi::LabelKey>& label_keys,
    psi::apsi_wrapper::YaclChannel& chl) {
  ::apsi::ThreadPoolMgr tpm;

  // Wait for query response
  ::apsi::QueryResponse response;
  bool logged_waiting = false;
  uint32_t retry_times = 0;

  while (!(response = ::apsi::to_query_response(chl.receive_response()))) {
    if (!logged_waiting) {
      // We want to log 'Waiting' only once, even if we have to wait for several
      // sleeps.
      logged_waiting = true;
      APSI_LOG_INFO("Waiting for response to query request");
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(kRecvRetryIntervalMs));

    if (++retry_times >= kRecvRetryTimes) {
      YACL_THROW("Sender failed to receive request from Receiver");
    }
  }

  if (options_.streaming_result) {
    // Set up the result
    std::vector<::apsi::receiver::MatchRecord> mrs(itt_.item_count());

    // Get the number of ResultPackages we expect to receive
    std::atomic<uint32_t> package_count{response->package_count};

    // Launch threads to receive ResultPackages and decrypt results
    uint64_t task_count = std::min<uint64_t>(
        ::apsi::ThreadPoolMgr::GetThreadCount(), package_count);
    std::vector<std::future<void>> futures(task_count);
    APSI_LOG_INFO("Launching " << task_count
                               << " result worker tasks to handle "
                               << package_count << " result parts");
    for (uint64_t t = 0; t < task_count; t++) {
      futures[t] = tpm.thread_pool().enqueue([&]() {
        process_result_worker(package_count, mrs, label_keys, itt_, chl);
      });
    }

    for (auto& f : futures) {
      f.get();
    }

    APSI_LOG_INFO("Found " << accumulate(mrs.begin(), mrs.end(), 0,
                                         [](auto acc, auto& curr) {
                                           return acc + curr.found;
                                         })
                           << " matches");

    return mrs;
  } else {
    APSI_LOG_INFO("streaming_result is off.");
    APSI_LOG_INFO("waiting for " << response->package_count << " result parts");

    auto seal_context = get_seal_context();

    std::vector<::apsi::ResultPart> rps;
    while (rps.size() < response->package_count) {
      rps.emplace_back(chl.receive_result(seal_context));
    }

    return process_result(label_keys, itt_, rps);
  }
}

heu::lib::algorithms::elgamal::Ciphertext DkPirReceiver::ComputeRowCountCt(
    const std::vector<::apsi::receiver::MatchRecord>& intersection) {
  std::string curve_name = psi::dkpir::FetchCurveName(options_.curve_type);
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

uint64_t DkPirReceiver::ComputeRowCount(
    const std::vector<::apsi::receiver::MatchRecord>& intersection) {
  uint64_t row_count = 0;
  std::string row_delimiter = std::string(1, kRowDelimiter);
  // Compute the total row count
  for (auto& mr : intersection) {
    if (mr.found) {
      std::string label = mr.label.to_string();
      std::vector<std::string> row_values =
          absl::StrSplit(label, row_delimiter);
      row_count += row_values.size();
    }
  }
  return row_count;
}

void DkPirReceiver::ReceiveShuffleSeed(
    std::shared_ptr<yacl::link::Context> lctx, uint128_t& shuffle_seed,
    uint64_t& shuffle_counter) {
  yacl::Buffer shuffle_seed_buf =
      lctx->Recv(lctx->NextRank(), "Recv shuffle seed");
  YACL_ENFORCE(shuffle_seed_buf.size() == sizeof(uint128_t));
  std::memcpy(&shuffle_seed, shuffle_seed_buf.data(), sizeof(uint128_t));

  yacl::Buffer shuffle_counter_buf =
      lctx->Recv(lctx->NextRank(), "Recv shuffle counter");
  YACL_ENFORCE(shuffle_counter_buf.size() == sizeof(uint64_t));
  std::memcpy(&shuffle_counter, shuffle_counter_buf.data(), sizeof(uint64_t));
}

uint64_t DkPirReceiver::SaveResult(
    const std::vector<::apsi::receiver::MatchRecord>& intersection,
    const std::vector<::apsi::Item>& items, uint128_t shuffle_seed,
    uint64_t shuffle_counter, const std::string& tmp_result_file) {
  psi::dkpir::WriteIntersectionResults(
      orig_items_, items, intersection, shuffle_seed, shuffle_counter,
      tmp_result_file, options_.skip_count_check);

  psi::ApsiCsvConverter receiver_result_converter(tmp_result_file, "key",
                                                  {"value"});
  uint64_t row_count =
      ::seal::util::safe_cast<uint64_t>(receiver_result_converter.ExtractResult(
          options_.result_file, options_.key, options_.labels));

  return row_count;
}
}  // namespace psi::dkpir
