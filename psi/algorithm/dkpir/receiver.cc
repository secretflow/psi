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

using namespace std::chrono_literals;

namespace psi::dkpir {
OPRFReceiver DkPirReceiver::CreateOPRFReceiver(
    const std::vector<::apsi::Item> &items) {
  STOPWATCH(::apsi::util::recv_stopwatch, "DkPirReceiver::CreateOPRFReceiver");

  OPRFReceiver oprf_receiver(items);
  APSI_LOG_INFO("Created OPRFReceiver for " << oprf_receiver.item_count()
                                            << " items");

  return oprf_receiver;
}

std::unique_ptr<::apsi::network::SenderOperation>
DkPirReceiver::CreateOPRFRequest(const OPRFReceiver &oprf_receiver,
                                 uint32_t bucket_idx) {
  auto sop = std::make_unique<::apsi::network::SenderOperationOPRF>();
  sop->data = oprf_receiver.query_data();
  sop->bucket_idx = bucket_idx;
  APSI_LOG_INFO("Created OPRF request for " << oprf_receiver.item_count()
                                            << " items");

  return sop;
}

std::pair<std::vector<::apsi::HashedItem>, std::vector<::apsi::LabelKey>>
DkPirReceiver::ExtractHashes(const ::apsi::OPRFResponse &oprf_response,
                             const OPRFReceiver &oprf_receiver) {
  STOPWATCH(::apsi::util::recv_stopwatch, "DkPirReceiver::ExtractHashes");

  if (!oprf_response) {
    APSI_LOG_ERROR(
        "Failed to extract OPRF hashes for items: oprf_response is null");
    return {};
  }

  auto response_size = oprf_response->data.size();
  uint64_t oprf_response_item_count =
      response_size / ::apsi::oprf::oprf_response_size;
  if ((response_size % ::apsi::oprf::oprf_response_size) ||
      (oprf_response_item_count != oprf_receiver.item_count())) {
    APSI_LOG_ERROR(
        "Failed to extract OPRF hashes for items: unexpected OPRF response "
        "size ("
        << response_size << " B)");
    return {};
  }

  std::vector<::apsi::HashedItem> items(oprf_receiver.item_count());
  std::vector<::apsi::LabelKey> label_keys(oprf_receiver.item_count());
  oprf_receiver.process_responses(oprf_response->data, items, label_keys);
  APSI_LOG_INFO("Extracted OPRF hashes for " << oprf_response_item_count
                                             << " items");

  return make_pair(std::move(items), std::move(label_keys));
}

std::pair<std::vector<::apsi::HashedItem>, std::vector<::apsi::LabelKey>>
DkPirReceiver::RequestOPRF(const std::vector<::apsi::Item> &items,
                           psi::apsi_wrapper::YaclChannel &chl,
                           uint32_t bucket_idx) {
  auto oprf_receiver = CreateOPRFReceiver(items);

  // Create OPRF request and send to Sender
  chl.send(CreateOPRFRequest(oprf_receiver, bucket_idx));

  if (items.empty()) {
    return {};
  }

  // Wait for a valid message of the right type
  ::apsi::OPRFResponse response;
  bool logged_waiting = false;
  while (!(response = ::apsi::to_oprf_response(chl.receive_response()))) {
    if (!logged_waiting) {
      // We want to log 'Waiting' only once, even if we have to wait for several
      // sleeps.
      logged_waiting = true;
      APSI_LOG_INFO("Waiting for response to OPRF request");
    }

    std::this_thread::sleep_for(50ms);
  }

  // Extract the OPRF hashed items
  return ExtractHashes(response, oprf_receiver);
}

std::vector<::apsi::receiver::MatchRecord> DkPirReceiver::ReceiveQuery(
    const std::vector<::apsi::LabelKey> &label_keys,
    const ::apsi::receiver::IndexTranslationTable &itt,
    psi::apsi_wrapper::YaclChannel &chl, bool streaming_result) {
  ::apsi::ThreadPoolMgr tpm;

  // Wait for query response
  ::apsi::QueryResponse response;
  bool logged_waiting = false;
  while (!(response = ::apsi::to_query_response(chl.receive_response()))) {
    if (!logged_waiting) {
      // We want to log 'Waiting' only once, even if we have to wait for several
      // sleeps.
      logged_waiting = true;
      APSI_LOG_INFO("Waiting for response to query request");
    }

    std::this_thread::sleep_for(50ms);
  }

  if (streaming_result) {
    // Set up the result
    std::vector<::apsi::receiver::MatchRecord> mrs(itt.item_count());

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
        process_result_worker(package_count, mrs, label_keys, itt, chl);
      });
    }

    for (auto &f : futures) {
      f.get();
    }

    APSI_LOG_INFO("Found " << accumulate(mrs.begin(), mrs.end(), 0,
                                         [](auto acc, auto &curr) {
                                           return acc + curr.found;
                                         })
                           << " matches");

    return mrs;
  } else {
    APSI_LOG_INFO("streaming_result is off.")
    APSI_LOG_INFO("waiting for " << response->package_count << " result parts");

    auto seal_context = get_seal_context();

    std::vector<::apsi::ResultPart> rps;
    while (rps.size() < response->package_count) {
      rps.emplace_back(chl.receive_result(seal_context));
    }

    return process_result(label_keys, itt, rps);
  }
}

std::vector<::apsi::receiver::MatchRecord> DkPirReceiver::RequestQuery(
    const std::vector<::apsi::HashedItem> &items,
    const std::vector<::apsi::LabelKey> &label_keys, uint128_t &shuffle_seed,
    uint64_t &shuffle_counter, psi::apsi_wrapper::YaclChannel &chl,
    bool streaming_result, uint32_t bucket_idx) {
  // Create query and send to Sender
  auto query = create_query(items, bucket_idx);
  chl.send(std::move(query.first));
  auto itt = std::move(query.second);

  std::vector<::apsi::receiver::MatchRecord> data_query_result,
      count_query_result;

  // Receiver processes the query result for the row count
  count_query_result = ReceiveQuery(label_keys, itt, chl, streaming_result);
  SPDLOG_INFO("Received the response for the row count query");

  // Receiver gets the public key of phe
  psi::dkpir::phe::PublicKey public_key = ReceivePublicKey(chl.get_lctx());

  // Receiver adds the ciphertexts of the row count and sends the result
  // to Sender
  SendRowCountCt(public_key, count_query_result, chl.get_lctx());

  // Receiver processes the query result for the data
  data_query_result = ReceiveQuery(label_keys, itt, chl, streaming_result);
  SPDLOG_INFO("Received the response for the data query");

  // Receiver computes the total row count of the data
  SendRowCount(data_query_result, chl.get_lctx());

  // Receiver gets the shuffle seed
  ReceiveShuffleSeed(chl.get_lctx(), shuffle_seed, shuffle_counter);

  return data_query_result;
}

psi::dkpir::phe::PublicKey DkPirReceiver::ReceivePublicKey(
    const std::shared_ptr<yacl::link::Context> &lctx) {
  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(
          "fourq", yacl::ArgLib = "FourQlib");
  uint64_t point_size = curve->GetSerializeLength();

  yacl::Buffer pk_buf = lctx->Recv(lctx->NextRank(), "Recv phe pk");
  YACL_ENFORCE(static_cast<uint64_t>(pk_buf.size()) == point_size);
  yacl::crypto::EcPoint pk_point = curve->DeserializePoint(pk_buf);

  SPDLOG_INFO("Received the public key of phe");

  return psi::dkpir::phe::PublicKey(pk_point, curve);
}

void DkPirReceiver::SendRowCountCt(
    const psi::dkpir::phe::PublicKey &public_key,
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    const std::shared_ptr<yacl::link::Context> &lctx) {
  auto curve = public_key.GetCurve();
  uint64_t ciphertext_size = curve->GetSerializeLength() * 2;
  psi::dkpir::phe::Encryptor encryptor(public_key);
  psi::dkpir::phe::Ciphertext row_count_ct = encryptor.EncryptZero();

  // Compute the ciphertext of the total row count
  for (auto &mr : intersection) {
    if (mr.found) {
      auto byte_span = mr.label.get_as<uint8_t>();
      YACL_ENFORCE(byte_span.size() == ciphertext_size);
      std::vector<uint8_t> byte_vector(byte_span.begin(), byte_span.end());

      psi::dkpir::phe::Ciphertext tmp_ct;
      tmp_ct.DeserializeCiphertext(curve, byte_vector.data(), ciphertext_size);

      row_count_ct =
          psi::dkpir::phe::Evaluator::Add(row_count_ct, tmp_ct, curve);
    }
  }

  // Send the ciphertext of the total row count
  std::vector<uint8_t> row_count_ct_buf(ciphertext_size);
  row_count_ct.SerializeCiphertext(curve, row_count_ct_buf.data(),
                                   ciphertext_size);

  lctx->SendAsync(lctx->NextRank(),
                  absl::MakeSpan(row_count_ct_buf.data(), ciphertext_size),
                  "Send ct of total row count");

  SPDLOG_INFO("Sent the ciphertext of the total row count");
}

void DkPirReceiver::SendRowCount(
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    const std::shared_ptr<yacl::link::Context> &lctx) {
  uint64_t row_count = 0;
  // Compute the total row count
  for (auto &mr : intersection) {
    if (mr.found) {
      std::string label = mr.label.to_string();
      std::vector<std::string> row_values = absl::StrSplit(label, "||");
      row_count += row_values.size();
    }
  }

  lctx->SendAsync(lctx->NextRank(),
                  yacl::ByteContainerView(&row_count, sizeof(uint64_t)),
                  "Send total row count");

  SPDLOG_INFO("Sent the plaintext of the total row count");
}

void DkPirReceiver::ReceiveShuffleSeed(
    const std::shared_ptr<yacl::link::Context> &lctx, uint128_t &shuffle_seed,
    uint64_t &shuffle_counter) {
  yacl::Buffer shuffle_seed_buf =
      lctx->Recv(lctx->NextRank(), "Recv shuffle seed");
  YACL_ENFORCE(shuffle_seed_buf.size() == sizeof(uint128_t));
  std::memcpy(&shuffle_seed, shuffle_seed_buf.data(), sizeof(uint128_t));

  yacl::Buffer shuffle_counter_buf =
      lctx->Recv(lctx->NextRank(), "Recv shuffle counter");
  YACL_ENFORCE(shuffle_counter_buf.size() == sizeof(uint64_t));
  std::memcpy(&shuffle_counter, shuffle_counter_buf.data(), sizeof(uint64_t));

  SPDLOG_INFO("Received the shuffle seed and shuffle counter");
}
}  // namespace psi::dkpir