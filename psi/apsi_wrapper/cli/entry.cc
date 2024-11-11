// Copyright 2024 Ant Group Co., Ltd.
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

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "psi/apsi_wrapper/cli/entry.h"

#include "spdlog/details/os.h"

// STD
#include <sched.h>
#include <sys/types.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spdlog/spdlog.h"

// APSI
#include "apsi/log.h"
#include "apsi/network/zmq/zmq_channel.h"
#include "apsi/thread_pool_mgr.h"

#include "psi/apsi_wrapper/cli/common_utils.h"
#include "psi/apsi_wrapper/cli/sender_dispatcher.h"
#include "psi/apsi_wrapper/receiver.h"
#include "psi/apsi_wrapper/utils/bucket.h"
#include "psi/apsi_wrapper/utils/common.h"
#include "psi/apsi_wrapper/utils/csv_reader.h"
#include "psi/apsi_wrapper/utils/group_db.h"
#include "psi/apsi_wrapper/utils/sender_db.h"
#include "psi/apsi_wrapper/yacl_channel.h"
#include "psi/utils/multiplex_disk_cache.h"
#include "psi/utils/resource_manager.h"

using namespace std;

namespace psi::apsi_wrapper::cli {

namespace {

void print_transmitted_data(::apsi::network::Channel &channel) {
  auto nice_byte_count = [](uint64_t bytes) -> std::string {
    std::stringstream ss;
    if (bytes >= 10 * 1024) {
      ss << bytes / 1024 << " KB";
    } else {
      ss << bytes << " B";
    }
    return ss.str();
  };

  SPDLOG_INFO("Communication R->S: {} ", nice_byte_count(channel.bytes_sent()));
  SPDLOG_INFO("Communication S->R: {}",
              nice_byte_count(channel.bytes_received()));
  SPDLOG_INFO("Communication total: {}",
              nice_byte_count(channel.bytes_sent() + channel.bytes_received()));
}

std::string get_conn_addr(const ReceiverOptions &options) {
  std::stringstream ss;
  ss << "tcp://" << options.zmq_ip_addr << ":" << options.zmq_port;

  return ss.str();
}

void sigint_handler(int param [[maybe_unused]]) {
  SPDLOG_WARN("Sender interrupted");
  print_timing_report(::apsi::util::sender_stopwatch);
  exit(0);
}

}  // namespace

int RunReceiver(const ReceiverOptions &options,
                const std::shared_ptr<yacl::link::Context> &lctx,
                int *match_cnt) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  std::unique_ptr<::apsi::network::NetworkChannel> channel;

  if (options.channel == "zmq") {
    // Connect to the network
    channel = std::make_unique<::apsi::network::ZMQReceiverChannel>();

    std::string conn_addr = get_conn_addr(options);
    SPDLOG_INFO("Connecting to {}", conn_addr);
    static_cast<::apsi::network::ZMQReceiverChannel *>(channel.get())
        ->connect(conn_addr);
    if (static_cast<::apsi::network::ZMQReceiverChannel *>(channel.get())
            ->is_connected()) {
      SPDLOG_INFO("Successfully connected to {}", conn_addr);
    } else {
      SPDLOG_WARN("Failed to connect to {}", conn_addr);
      return -1;
    }
  } else {
    if (lctx) {
      lctx->ConnectToMesh();

      channel = std::make_unique<psi::apsi_wrapper::YaclChannel>(lctx);
    } else {
      yacl::link::ContextDesc ctx_desc;

      ctx_desc.parties.emplace_back(
          "sender", fmt::format("{}:{}", options.yacl_sender_ip_addr,
                                options.yacl_sender_port));
      ctx_desc.parties.emplace_back(
          "receiver", fmt::format("{}:{}", options.yacl_receiver_ip_addr,
                                  options.yacl_receiver_port));

      auto link = psi::ResourceManager::GetInstance().AddLinkResource("sender",
                                                                      ctx_desc);

      auto lctx = link->GetLinkContext();
      lctx->ConnectToMesh();

      channel = std::make_unique<psi::apsi_wrapper::YaclChannel>(lctx);
    }
  }

  // reciver must own the same params_file as sender.
  unique_ptr<::apsi::PSIParams> params =
      psi::apsi_wrapper::BuildPsiParams(options.params_file);

  if (!params) {
    try {
      SPDLOG_INFO("Sending parameter request");
      params = make_unique<::apsi::PSIParams>(
          psi::apsi_wrapper::Receiver::RequestParams(*channel));
      SPDLOG_INFO("Received valid parameters");
    } catch (const exception &ex) {
      SPDLOG_WARN("Failed to receive valid parameters: {}", ex.what());
      return -1;
    }
  }

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());

  psi::apsi_wrapper::Receiver receiver(*params);

  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(options.query_file);

  if (!query_data ||
      !holds_alternative<psi::apsi_wrapper::UnlabeledData>(*query_data)) {
    // Failed to read query file
    SPDLOG_ERROR("Failed to read query file: terminating");
    return -1;
  }

  auto &items = get<psi::apsi_wrapper::UnlabeledData>(*query_data);

  if (options.experimental_enable_bucketize) {
    std::unordered_map<
        size_t, std::pair<std::vector<::apsi::Item>, std::vector<std::string>>>
        bucket_item_map;

    for (size_t i = 0; i < orig_items.size(); i++) {
      int bucket_idx = std::hash<std::string>()(orig_items[i]) %
                       options.experimental_bucket_cnt;

      if (bucket_item_map.find(bucket_idx) == bucket_item_map.end()) {
        bucket_item_map[bucket_idx] = std::make_pair(
            std::vector<::apsi::Item>(), std::vector<std::string>());
      }

      bucket_item_map[bucket_idx].first.emplace_back(items[i]);
      bucket_item_map[bucket_idx].second.emplace_back(orig_items[i]);
    }

    bool append_to_outfile = false;
    size_t total_matches = 0;

    double total_time = 0;
    for (const auto &pair : bucket_item_map) {
      const size_t bucket_idx = pair.first;
      const vector<::apsi::Item> &items_vec = pair.second.first;
      auto bucket_start = std::chrono::high_resolution_clock::now();
      SPDLOG_INFO("Start deal with bucket {}", bucket_idx);

      vector<::apsi::HashedItem> oprf_items;
      vector<::apsi::LabelKey> label_keys;
      try {
        SPDLOG_INFO("Sending OPRF request for {} {}", items_vec.size(),
                    " items ");
        tie(oprf_items, label_keys) = psi::apsi_wrapper::Receiver::RequestOPRF(
            items_vec, *channel, bucket_idx);
        SPDLOG_INFO("Received OPRF response for {}  items", items_vec.size());
      } catch (const exception &ex) {
        SPDLOG_WARN("OPRF request failed: {}", ex.what());
        return -1;
      }

      vector<::apsi::receiver::MatchRecord> query_result;
      try {
        SPDLOG_INFO("Sending APSI query");
        query_result =
            receiver.request_query(oprf_items, label_keys, *channel,
                                   options.streaming_result, bucket_idx);
        SPDLOG_INFO("Received APSI query response");
      } catch (const exception &ex) {
        SPDLOG_WARN("Failed sending APSI query: {}", ex.what());
        return -1;
      }

      auto bucket_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::high_resolution_clock::now() - bucket_start)
              .count();
      total_time += bucket_time;
      SPDLOG_INFO("End deal with bucket {}, time: {}ms", bucket_idx,
                  bucket_time);

      int cnt = psi::apsi_wrapper::print_intersection_results(
          pair.second.second, items_vec, query_result, options.output_file,
          append_to_outfile);

      if (cnt > 0 && !append_to_outfile) {
        append_to_outfile = true;
      }

      total_matches += cnt;
    }

    SPDLOG_INFO("Average bucket time: {}ms/bucket",
                total_time / bucket_item_map.size());

    if (match_cnt != nullptr) {
      *match_cnt = total_matches;
    }

    SPDLOG_INFO("Total matches {}  items.", total_matches);

    print_transmitted_data(*channel);
    print_timing_report(::apsi::util::recv_stopwatch);

  } else {
    vector<::apsi::Item> items_vec(items.begin(), items.end());
    vector<::apsi::HashedItem> oprf_items;
    vector<::apsi::LabelKey> label_keys;
    try {
      SPDLOG_INFO("Sending OPRF request for {} items ", items_vec.size());
      tie(oprf_items, label_keys) =
          psi::apsi_wrapper::Receiver::RequestOPRF(items_vec, *channel);
      SPDLOG_INFO("Received OPRF response for {} items", items_vec.size());
    } catch (const exception &ex) {
      SPDLOG_WARN("OPRF request failed: {}", ex.what());
      return -1;
    }

    vector<::apsi::receiver::MatchRecord> query_result;
    try {
      SPDLOG_INFO("Sending APSI query");
      query_result = receiver.request_query(oprf_items, label_keys, *channel,
                                            options.streaming_result);
      SPDLOG_INFO("Received APSI query response");
    } catch (const exception &ex) {
      SPDLOG_WARN("Failed sending APSI query: {}", ex.what());
      return -1;
    }

    int cnt = psi::apsi_wrapper::print_intersection_results(
        orig_items, items_vec, query_result, options.output_file);

    if (match_cnt != nullptr) {
      *match_cnt = cnt;
    }

    print_transmitted_data(*channel);
    print_timing_report(::apsi::util::recv_stopwatch);
  }

  // NOTE(junfeng): Yacl channel need to send a empty oprf request with max
  // bucket_idx to stop.
  if (options.channel == "yacl") {
    psi::apsi_wrapper::Receiver::RequestOPRF(
        {}, *channel, std::numeric_limits<uint32_t>::max());
  }

  return 0;
}

template <typename... Args>
void RunDispatcher(const SenderOptions &options,
                   const std::shared_ptr<yacl::link::Context> &lctx,
                   Args &&...args) {
  atomic<bool> stop = false;
  SenderDispatcher dispatcher(std::forward<Args &&>(args)...);

  if (options.channel == "zmq") {
    // The dispatcher will run until stopped.
    dispatcher.run(stop, options.zmq_port, options.streaming_result);
  } else {
    if (lctx) {
      lctx->ConnectToMesh();

      dispatcher.run(stop, lctx, options.streaming_result);
    } else {
      yacl::link::ContextDesc ctx_desc;
      ctx_desc.parties.emplace_back(
          "sender", fmt::format("{}:{}", options.yacl_sender_ip_addr,
                                options.yacl_sender_port));
      ctx_desc.parties.emplace_back(
          "receiver", fmt::format("{}:{}", options.yacl_receiver_ip_addr,
                                  options.yacl_receiver_port));

      auto link = psi::ResourceManager::GetInstance().AddLinkResource(
          "receiver", ctx_desc);

      auto lctx = link->GetLinkContext();
      lctx->ConnectToMesh();

      dispatcher.run(stop, lctx, options.streaming_result);
    }
  }
}

void LogSenderDBInfo(shared_ptr<::apsi::sender::SenderDB> sender_db) {
  // Print the total number of bin bundles and the largest number of bin
  // bundles for any bundle index
  uint32_t max_bin_bundles_per_bundle_idx = 0;
  for (uint32_t bundle_idx = 0;
       bundle_idx < sender_db->get_params().bundle_idx_count(); bundle_idx++) {
    max_bin_bundles_per_bundle_idx =
        max(max_bin_bundles_per_bundle_idx,
            static_cast<uint32_t>(sender_db->get_bin_bundle_count(bundle_idx)));
  }
  SPDLOG_INFO(
      "SenderDB holds a total of {} bin bundles across {} bundle indices",
      sender_db->get_bin_bundle_count(),
      sender_db->get_params().bundle_idx_count());
  SPDLOG_INFO("The largest bundle index holds {} bin bundles",
              max_bin_bundles_per_bundle_idx);
}

void DealSingleDB(const SenderOptions &options,
                  const std::shared_ptr<yacl::link::Context> &lctx) {
  // Check that the database file is valid
  YACL_ENFORCE(!(options.db_file.empty() && options.source_file.empty()),
               "Both old db_file and source_file are empty.");

  // Try loading first as a SenderDB, then as a CSV file
  ::apsi::oprf::OPRFKey oprf_key;
  shared_ptr<::apsi::sender::SenderDB> sender_db;
  if (!options.db_file.empty()) {
    sender_db = psi::apsi_wrapper::TryLoadSenderDB(
        options.db_file, options.params_file, oprf_key);
    YACL_ENFORCE(sender_db != nullptr, "load old sender_db from {} failed",
                 options.db_file);
  } else {
    sender_db = psi::apsi_wrapper::GenerateSenderDB(
        options.source_file, options.params_file, options.nonce_byte_count,
        options.compress, oprf_key);
    YACL_ENFORCE(sender_db != nullptr, "create sender_db from {} failed",
                 options.source_file);
  }

  LogSenderDBInfo(sender_db);

  // Try to save the SenderDB if a save file was given
  if (!options.sdb_out_file.empty()) {
    YACL_ENFORCE(psi::apsi_wrapper::TrySaveSenderDB(options.sdb_out_file,
                                                    sender_db, oprf_key),
                 "Save sender_db to {} failed.", options.sdb_out_file);
  }

  if (options.save_db_only) {
    SPDLOG_INFO("Save db only. Exiting...");
    return;
  }

  // Run the dispatcher
  RunDispatcher(options, lctx, sender_db, oprf_key);
}

void DealGroupBucketDB(const SenderOptions &options,
                       const std::shared_ptr<yacl::link::Context> &lctx) {
  YACL_ENFORCE(!options.experimental_bucket_folder.empty(),
               "experimental_bucket_folder is not provided.");

  if (!std::filesystem::exists(options.experimental_bucket_folder)) {
    SPDLOG_INFO("Creating bucket folder {}",
                options.experimental_bucket_folder);
    std::filesystem::create_directories(options.experimental_bucket_folder);
  }

  GroupDB group_db(options.source_file, options.experimental_bucket_folder,
                   options.experimental_bucket_group_cnt,
                   options.experimental_bucket_cnt, options.nonce_byte_count,
                   options.params_file, options.compress);

  if (!group_db.IsDBGenerated()) {
    YACL_ENFORCE(!options.source_file.empty() &&
                     std::filesystem::exists(options.source_file),
                 "source file {} is not exist.", options.source_file);

    auto start = std::chrono::high_resolution_clock::now();
    SPDLOG_INFO("start Generate bucket DB");

    GenerateGroupBucketDB(group_db,
                          options.experimental_db_generating_process_num);

    auto sum_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::high_resolution_clock::now() - start)
                            .count();
    SPDLOG_INFO(
        "end Generate bucket DB: {}ms, {}ms/bucket, {}h/10^6bucket",
        sum_duration,
        static_cast<double>(sum_duration) / options.experimental_bucket_cnt,
        static_cast<double>(sum_duration) / options.experimental_bucket_cnt *
            1e6 / 1000 / 3600);
  }

  if (options.save_db_only) {
    SPDLOG_INFO("Save db only. Exiting...");
    return;
  }

  // Run the dispatcher
  RunDispatcher(options, lctx, group_db);
}

int RunSender(const SenderOptions &options,
              const std::shared_ptr<yacl::link::Context> &lctx) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  SPDLOG_INFO("Setting thread count to {}",
              ::apsi::ThreadPoolMgr::GetThreadCount());
  signal(SIGINT, sigint_handler);

  // single db
  if (!options.experimental_enable_bucketize) {
    DealSingleDB(options, lctx);

  } else {  // bucket db
    DealGroupBucketDB(options, lctx);
  }

  return 0;
}

}  // namespace psi::apsi_wrapper::cli
