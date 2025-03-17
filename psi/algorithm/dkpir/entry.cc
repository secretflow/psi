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
#include "psi/algorithm/dkpir/receiver.h"
#include "psi/algorithm/dkpir/sender.h"
#include "psi/algorithm/dkpir/sender_cnt_db.h"
#include "psi/algorithm/dkpir/sender_dispatcher.h"
#include "psi/utils/csv_converter.h"
#include "psi/utils/random_str.h"
#include "psi/wrapper/apsi/cli/common_utils.h"

namespace psi::dkpir {
int SenderOffline(const DkPirSenderOptions &options) {
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

  psi::ApsiCsvConverter sender_converter(options.source_file, options.key,
                                         options.labels);

  sender_converter.MergeColumnAndRow(key_value_file, key_count_file);

  SPDLOG_INFO(
      "Sender created two temporary files, one for the data table and the "
      "other for the row count table");

  ::apsi::oprf::OPRFKey oprf_key;
  std::shared_ptr<::apsi::sender::SenderDB> sender_db;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db;

  // Generate SenderDB (for data)
  sender_db = psi::apsi_wrapper::GenerateSenderDB(
      key_value_file, options.params_file, options.nonce_byte_count,
      options.compress, oprf_key);
  YACL_ENFORCE(sender_db != nullptr, "Create sender_db from {} failed",
               key_value_file);

  // Generate SenderCntDB (for row count)
  sender_cnt_db = psi::dkpir::GenerateSenderCntDB(
      key_count_file, options.params_file, options.secret_key_file,
      options.nonce_byte_count, options.compress, oprf_key);
  YACL_ENFORCE(sender_cnt_db != nullptr, "Create sender_cnt_db from {} failed",
               key_count_file);

  // Save the sender_db if a save file was given
  YACL_ENFORCE(psi::apsi_wrapper::TrySaveSenderDB(options.value_sdb_out_file,
                                                  sender_db, oprf_key),
               "Save sender_db to {} failed.", options.value_sdb_out_file);

  // Save the sender_cnt_db using the reusable method TrySaveSenderDB
  YACL_ENFORCE(psi::apsi_wrapper::TrySaveSenderDB(options.count_sdb_out_file,
                                                  sender_cnt_db, oprf_key),
               "Save sender_cnt_db to {} failed.", options.count_sdb_out_file);

  SPDLOG_INFO("Sender saved sender_db and sender_cnt_db");
  return 0;
}

int SenderOnline(const DkPirSenderOptions &options,
                 const std::shared_ptr<yacl::link::Context> &lctx) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());

  ::apsi::oprf::OPRFKey oprf_key;
  std::shared_ptr<::apsi::sender::SenderDB> sender_db;
  std::shared_ptr<::apsi::sender::SenderDB> sender_cnt_db;

  sender_db = psi::apsi_wrapper::TryLoadSenderDB(options.value_sdb_out_file,
                                                 options.params_file, oprf_key);
  YACL_ENFORCE(sender_db != nullptr, "Load old sender_db from {} failed",
               options.value_sdb_out_file);

  // Here, we reuse the method TryLoadSenderDB, which means that oprf_key will
  // be read twice. But since these two databases actually use the same
  // oprf_key, it doesn't matter.
  sender_cnt_db = psi::apsi_wrapper::TryLoadSenderDB(
      options.count_sdb_out_file, options.params_file, oprf_key);

  YACL_ENFORCE(sender_cnt_db != nullptr,
               "Load old sender_cnt_db from {} failed",
               options.count_sdb_out_file);

  SPDLOG_INFO("Sender loaded sender_db and sender_cnt_db");

  std::atomic<bool> stop = false;

  psi::dkpir::DkPirSenderDispatcher dispatcher(
      sender_db, sender_cnt_db, oprf_key, options.secret_key_file,
      options.result_file);

  lctx->ConnectToMesh();
  dispatcher.run(stop, lctx, options.streaming_result);

  return 0;
}

int ReceiverOnline(const DkPirReceiverOptions &options,
                   const std::shared_ptr<yacl::link::Context> &lctx) {
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
  std::string apsi_output_file =
      tmp_dir / fmt::format("apsi_output_{}.csv", uuid_str);

  psi::ApsiCsvConverter receiver_query_converter(options.query_file,
                                                 options.key);
  receiver_query_converter.ExtractQuery(tmp_query_file);
  SPDLOG_INFO("Store the extracted query in {}", tmp_query_file);

  lctx->ConnectToMesh();

  psi::apsi_wrapper::YaclChannel channel(lctx);

  // Reciver must own the same params_file as sender.
  std::unique_ptr<::apsi::PSIParams> params =
      psi::apsi_wrapper::BuildPsiParams(options.params_file);

  if (!params) {
    try {
      SPDLOG_INFO("Sending parameter request");
      params = std::make_unique<::apsi::PSIParams>(
          psi::apsi_wrapper::Receiver::RequestParams(channel));
      SPDLOG_INFO("Received valid parameters");
    } catch (const std::exception &ex) {
      SPDLOG_WARN("Failed to receive valid parameters: {}", ex.what());
      return -1;
    }
  }

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());

  DkPirReceiver receiver(*params);

  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(tmp_query_file);

  if (!query_data ||
      !std::holds_alternative<psi::apsi_wrapper::UnlabeledData>(*query_data)) {
    // Failed to read query file
    SPDLOG_ERROR("Failed to read query file: terminating");
    return -1;
  }

  auto &items = std::get<psi::apsi_wrapper::UnlabeledData>(*query_data);

  std::vector<::apsi::Item> items_vec(items.begin(), items.end());
  std::vector<::apsi::HashedItem> oprf_items;
  std::vector<::apsi::LabelKey> label_keys;

  try {
    SPDLOG_INFO("Sending OPRF request for {} items ", items_vec.size());
    // psi::apsi_wrapper::Receiver::RequestOPRF doesn't support shuffling, but
    // psi::dkpir::DkPirReceiver::RequestOPRF does.
    tie(oprf_items, label_keys) =
        DkPirReceiver::RequestOPRF(items_vec, channel);
    SPDLOG_INFO("Received OPRF response for {} items", items_vec.size());
  } catch (const std::exception &ex) {
    SPDLOG_WARN("OPRF request failed: {}", ex.what());
    return -1;
  }

  std::vector<::apsi::receiver::MatchRecord> query_result;
  uint128_t shuffle_seed = 0;
  uint64_t shuffle_counter = 0;
  try {
    SPDLOG_INFO("Sending DkPir query");
    query_result = receiver.RequestQuery(oprf_items, label_keys, shuffle_seed,
                                         shuffle_counter, channel,
                                         options.streaming_result);
    SPDLOG_INFO("Received DkPir query response");
  } catch (const std::exception &ex) {
    SPDLOG_WARN("Failed sending DkPir query: {}", ex.what());
    return -1;
  }

  PrintIntersectionResults(orig_items, items_vec, query_result, shuffle_seed,
                           shuffle_counter, apsi_output_file);

  PrintTransmittedData(channel);
  psi::apsi_wrapper::cli::print_timing_report(::apsi::util::recv_stopwatch);

  // NOTE(junfeng): Yacl channel need to send a empty oprf request with max
  // bucket_idx to stop.
  DkPirReceiver::RequestOPRF({}, channel, std::numeric_limits<uint32_t>::max());

  // Receiver convert result file
  psi::ApsiCsvConverter recevier_result_converter(apsi_output_file, "key",
                                                  {"value"});

  uint64_t row_count =
      ::seal::util::safe_cast<uint64_t>(recevier_result_converter.ExtractResult(
          options.result_file, options.key, options.labels));

  SPDLOG_INFO("Receiver has received {} rows in total.", row_count);

  return 0;
}

}  // namespace psi::dkpir