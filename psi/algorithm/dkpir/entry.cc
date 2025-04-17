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

#include "psi/algorithm/dkpir/entry.h"

#include <filesystem>
#include <string>

#include "psi/algorithm/dkpir/common.h"
#include "psi/utils/random_str.h"
#include "psi/wrapper/apsi/cli/common_utils.h"

namespace psi::dkpir {
int SenderOffline(const DkPirSenderOptions &options) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  YACL_ENFORCE(!options.tmp_folder.empty(),
               "The folder for storing temporary files is not provided.");

  if (!std::filesystem::exists(options.tmp_folder)) {
    SPDLOG_INFO("Creating tmp folder {}", options.tmp_folder);
    std::filesystem::create_directories(options.tmp_folder);
  }
  std::filesystem::path tmp_dir(options.tmp_folder);

  auto uuid_str = GetRandomString();

  std::string key_value_file =
      tmp_dir / fmt::format("key_value_{}.csv", uuid_str);
  std::string key_count_file =
      tmp_dir / fmt::format("key_count_{}.csv", uuid_str);

  DkPirSender sender(options);

  sender.PreProcessData(key_value_file, key_count_file);

  sender.GenerateDB(key_value_file, key_count_file);

  RemoveTempFile(key_value_file);
  if (!options.skip_count_check) {
    RemoveTempFile(key_count_file);
  }

  return 0;
}

int SenderOnline(const DkPirSenderOptions &options,
                 std::shared_ptr<yacl::link::Context> lctx) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());

  DkPirSender sender(options);

  // Sender loads sender_db (and sender_cnt_db)
  sender.LoadDB();

  if (!options.skip_count_check) {
    try {
      sender.LoadSecretKey();
      SPDLOG_INFO(
          "Sender loaded the secret key and the random linear function");
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("Sender threw an exception while loading secret key: {}",
                   ex.what());
    }
  }

  lctx->ConnectToMesh();

  psi::apsi_wrapper::YaclChannel channel(lctx);

  try {
    ::apsi::Request oprf_request = sender.ReceiveRequest(channel);
    SPDLOG_INFO("Sender received OPRF request");

    ::apsi::OPRFResponse oprf_response =
        sender.RunOPRF(std::move(oprf_request));

    channel.send(std::move(oprf_response));
    SPDLOG_INFO("Finished processing the OPRF query and shuffled the results");
  } catch (const std::exception &ex) {
    SPDLOG_ERROR("Sender threw an exception while processing OPRF request: {}",
                 ex.what());
    return -1;
  }

  if (options.skip_count_check) {
    SPDLOG_INFO("In this case, skip count check");

    try {
      ::apsi::Request request = sender.ReceiveRequest(channel);
      SPDLOG_INFO("Sender received query request");

      auto query_request = ::apsi::to_query_request(std::move(request));

      // Create the DkPirQuery object which only includes one database for data
      DkPirQuery query(std::move(query_request), sender.GetSenderDB());

      // Sender only processes the data query
      sender.RunQuery(query, channel, false);
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("Sender threw an exception while processing query: {}",
                   ex.what());
      return -1;
    }

  } else {
    SPDLOG_INFO("In this case, do count check");

    try {
      ::apsi::Request request = sender.ReceiveRequest(channel);
      SPDLOG_INFO("Sender received query request");

      auto query_request = ::apsi::to_query_request(std::move(request));

      // Create the DkPirQuery object which includes two databases
      DkPirQuery query(std::move(query_request), sender.GetSenderDB(),
                       sender.GetSenderCntDB());

      // Sender processes the row count query
      sender.RunQuery(query, channel, true);

      heu::lib::algorithms::elgamal::Ciphertext row_count_ct =
          sender.ReceiveRowCountCt(lctx);
      SPDLOG_INFO("Sender received the ciphertext of the total row count");

      // Sender processes the data query
      sender.RunQuery(query, channel, false);

      uint64_t row_count = sender.ReceiveRowCount(lctx);
      SPDLOG_INFO("Sender received the plaintext of the total row count");

      YACL_ENFORCE(sender.CheckRowCount(row_count_ct, row_count),
                   "Check row count failed");

      sender.SaveRowCount(row_count);
      SPDLOG_INFO(
          "The verification of the total row count was successful, the total "
          "row count was {}, and the result was stored in {}",
          row_count, options.result_file);

      uint128_t shuffle_seed = sender.GetShuffleSeed();
      uint64_t shuffle_counter = sender.GetShuffleCounter();

      lctx->SendAsync(lctx->NextRank(),
                      yacl::ByteContainerView(&shuffle_seed, sizeof(uint128_t)),
                      "Send shuffle seed");
      lctx->SendAsync(
          lctx->NextRank(),
          yacl::ByteContainerView(&shuffle_counter, sizeof(uint64_t)),
          "Send shuffle counter");
      SPDLOG_INFO("Sender sent the shuffle seed and counter");
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("Sender threw an exception while processing query: {}",
                   ex.what());
      return -1;
    }
  }

  return 0;
}

int ReceiverOnline(const DkPirReceiverOptions &options,
                   std::shared_ptr<yacl::link::Context> lctx) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  YACL_ENFORCE(!options.tmp_folder.empty(),
               "The folder for storing temporary files is not provided.");

  if (!std::filesystem::exists(options.tmp_folder)) {
    SPDLOG_INFO("Create tmp folder {}", options.tmp_folder);
    std::filesystem::create_directories(options.tmp_folder);
  }
  std::filesystem::path tmp_dir(options.tmp_folder);

  auto uuid_str = GetRandomString();

  std::string tmp_query_file =
      tmp_dir / fmt::format("tmp_query_{}.csv", uuid_str);
  std::string tmp_result_file =
      tmp_dir / fmt::format("tmp_result_{}.csv", uuid_str);

  std::unique_ptr<::apsi::PSIParams> params =
      psi::apsi_wrapper::BuildPsiParams(options.params_file);

  if (!params) {
    SPDLOG_ERROR("Failed to read params file: terminating");
    return -1;
  }

  DkPirReceiver receiver(*params, options);

  std::vector<::apsi::Item> items;
  try {
    items = receiver.ExtractItems(tmp_query_file);
    SPDLOG_INFO("Receiver extracted {} items from the query file",
                items.size());
  } catch (const std::exception &ex) {
    SPDLOG_ERROR("Receiver extracted query failed: {}", ex.what());
    return -1;
  }

  lctx->ConnectToMesh();

  psi::apsi_wrapper::YaclChannel channel(lctx);

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());

  std::vector<::apsi::HashedItem> oprf_items;
  std::vector<::apsi::LabelKey> label_keys;

  std::vector<::apsi::receiver::MatchRecord> data_query_result,
      count_query_result;

  uint128_t shuffle_seed = 0;
  uint64_t shuffle_counter = 0;
  uint64_t row_count = 0;

  if (options.skip_count_check) {
    SPDLOG_INFO("In this case, skip count check");
    try {
      ::apsi::oprf::OPRFReceiver oprf_receiver =
          psi::apsi_wrapper::Receiver::CreateOPRFReceiver(items);

      ::apsi::Request oprf_request =
          psi::apsi_wrapper::Receiver::CreateOPRFRequest(oprf_receiver);

      channel.send(std::move(oprf_request));
      SPDLOG_INFO("Receiver sent OPRF request for {} items ", items.size());

      ::apsi::OPRFResponse oprf_response =
          receiver.ReceiveOPRFResponse(channel);

      tie(oprf_items, label_keys) = psi::apsi_wrapper::Receiver::ExtractHashes(
          oprf_response, oprf_receiver);

      SPDLOG_INFO("Receiver received OPRF response for {} items", items.size());
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("OPRF request failed: {}", ex.what());
      return -1;
    }

    try {
      ::apsi::Request query_request = receiver.CreateQueryRequest(oprf_items);
      channel.send(std::move(query_request));
      SPDLOG_INFO("Receiver sent DkPir query");

      // In this case, Receiver only needs to process the query result for data
      data_query_result = receiver.ReceiveQueryResponse(label_keys, channel);
      SPDLOG_INFO("Receiver received the response for the data query");
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("Failed processing DkPir query: {}", ex.what());
      return -1;
    }

  } else {
    SPDLOG_INFO("In this case, do count check");
    try {
      ShuffledOPRFReceiver shuffled_oprf_receiver =
          receiver.CreateShuffledOPRFReceiver(items);

      ::apsi::Request oprf_request =
          receiver.CreateOPRFRequest(shuffled_oprf_receiver);

      channel.send(std::move(oprf_request));
      SPDLOG_INFO("Receiver sent OPRF request for {} items ", items.size());

      ::apsi::OPRFResponse oprf_response =
          receiver.ReceiveOPRFResponse(channel);

      tie(oprf_items, label_keys) =
          receiver.ExtractHashes(oprf_response, shuffled_oprf_receiver);

      SPDLOG_INFO("Receiver received OPRF response for {} items", items.size());
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("OPRF request failed: {}", ex.what());
      return -1;
    }

    try {
      ::apsi::Request query_request = receiver.CreateQueryRequest(oprf_items);
      channel.send(std::move(query_request));
      SPDLOG_INFO("Receiver sent DkPir query");

      // Receiver processes the query result for the row count
      count_query_result = receiver.ReceiveQueryResponse(label_keys, channel);
      SPDLOG_INFO("Receiver received the response for the row count query");

      // Receiver adds the ciphertexts of the row count and sends the result
      // to Sender
      heu::lib::algorithms::elgamal::Ciphertext row_count_ct =
          receiver.ComputeRowCountCt(count_query_result);

      yacl::Buffer row_count_ct_buf = row_count_ct.Serialize(true);
      lctx->SendAsync(lctx->NextRank(), row_count_ct_buf,
                      "Send ct of total row count");
      SPDLOG_INFO("Receiver sent the ciphertext of the total row count");

      // Receiver processes the query result for the data
      data_query_result = receiver.ReceiveQueryResponse(label_keys, channel);
      SPDLOG_INFO("Receiver received the response for the data query");

      // Receiver computes and sends the total row count of the data
      uint64_t total_row_count = receiver.ComputeRowCount(data_query_result);

      lctx->SendAsync(
          lctx->NextRank(),
          yacl::ByteContainerView(&total_row_count, sizeof(uint64_t)),
          "Send total row count");
      SPDLOG_INFO("Receiver sent the plaintext of the total row count");

      // Receiver gets the shuffle seed
      receiver.ReceiveShuffleSeed(lctx, shuffle_seed, shuffle_counter);
      SPDLOG_INFO("Receiver received the shuffle seed and shuffle counter");
    } catch (const std::exception &ex) {
      SPDLOG_ERROR("Failed processing DkPir query: {}", ex.what());
      return -1;
    }
  }

  row_count = receiver.SaveResult(data_query_result, items, shuffle_seed,
                                  shuffle_counter, tmp_result_file);

  PrintTransmittedData(channel);
  psi::apsi_wrapper::cli::print_timing_report(::apsi::util::recv_stopwatch);
  SPDLOG_INFO("Receiver has received {} rows in total.", row_count);

  RemoveTempFile(tmp_query_file);
  RemoveTempFile(tmp_result_file);

  return 0;
}

}  // namespace psi::dkpir