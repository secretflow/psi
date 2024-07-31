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

// STD
#include <sys/types.h>

#include <csignal>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
#include "psi/apsi_wrapper/utils/sender_db.h"
#include "psi/apsi_wrapper/yacl_channel.h"
#include "psi/utils/multiplex_disk_cache.h"

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

  APSI_LOG_INFO(
      "Communication R->S: " << nice_byte_count(channel.bytes_sent()));
  APSI_LOG_INFO(
      "Communication S->R: " << nice_byte_count(channel.bytes_received()));
  APSI_LOG_INFO("Communication total: " << nice_byte_count(
                    channel.bytes_sent() + channel.bytes_received()));
}

std::string get_conn_addr(const ReceiverOptions &options) {
  std::stringstream ss;
  ss << "tcp://" << options.zmq_ip_addr << ":" << options.zmq_port;

  return ss.str();
}

void sigint_handler(int param [[maybe_unused]]) {
  APSI_LOG_WARNING("Sender interrupted");
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
    APSI_LOG_INFO("Connecting to " << conn_addr);
    static_cast<::apsi::network::ZMQReceiverChannel *>(channel.get())
        ->connect(conn_addr);
    if (static_cast<::apsi::network::ZMQReceiverChannel *>(channel.get())
            ->is_connected()) {
      APSI_LOG_INFO("Successfully connected to " << conn_addr);
    } else {
      APSI_LOG_WARNING("Failed to connect to " << conn_addr);
      return -1;
    }
  } else {
    if (lctx) {
      lctx->ConnectToMesh();

      channel = std::make_unique<psi::apsi_wrapper::YaclChannel>(lctx);
    } else {
      yacl::link::ContextDesc ctx_desc;
      ctx_desc.parties.push_back(
          {"sender", fmt::format("{}:{}", options.yacl_sender_ip_addr,
                                 options.yacl_sender_port)});
      ctx_desc.parties.push_back(
          {"receiver", fmt::format("{}:{}", options.yacl_receiver_ip_addr,
                                   options.yacl_receiver_port)});

      std::shared_ptr<yacl::link::Context> lctx_ =
          yacl::link::FactoryBrpc().CreateContext(ctx_desc, 1);

      lctx_->ConnectToMesh();

      channel = std::make_unique<psi::apsi_wrapper::YaclChannel>(lctx_);
    }
  }

  // reciver must own the same params_file as sender.
  unique_ptr<::apsi::PSIParams> params =
      psi::apsi_wrapper::build_psi_params(options.params_file);

  if (!params) {
    try {
      APSI_LOG_INFO("Sending parameter request");
      params = make_unique<::apsi::PSIParams>(
          psi::apsi_wrapper::Receiver::RequestParams(*channel));
      APSI_LOG_INFO("Received valid parameters");
    } catch (const exception &ex) {
      APSI_LOG_WARNING("Failed to receive valid parameters: " << ex.what());
      return -1;
    }
  }

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  APSI_LOG_INFO("Setting thread count to "
                << ::apsi::ThreadPoolMgr::GetThreadCount());

  psi::apsi_wrapper::Receiver receiver(*params);

  auto [query_data, orig_items] =
      psi::apsi_wrapper::load_db_with_orig_items(options.query_file);

  if (!query_data ||
      !holds_alternative<psi::apsi_wrapper::UnlabeledData>(*query_data)) {
    // Failed to read query file
    APSI_LOG_ERROR("Failed to read query file: terminating");
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

    for (const auto &pair : bucket_item_map) {
      const size_t bucket_idx = pair.first;
      const vector<::apsi::Item> &items_vec = pair.second.first;

      vector<::apsi::HashedItem> oprf_items;
      vector<::apsi::LabelKey> label_keys;
      try {
        APSI_LOG_INFO("Sending OPRF request for " << items_vec.size()
                                                  << " items ");
        tie(oprf_items, label_keys) = psi::apsi_wrapper::Receiver::RequestOPRF(
            items_vec, *channel, bucket_idx);
        APSI_LOG_INFO("Received OPRF response for " << items_vec.size()
                                                    << " items");
      } catch (const exception &ex) {
        APSI_LOG_WARNING("OPRF request failed: " << ex.what());
        return -1;
      }

      vector<::apsi::receiver::MatchRecord> query_result;
      try {
        APSI_LOG_INFO("Sending APSI query");
        query_result =
            receiver.request_query(oprf_items, label_keys, *channel,
                                   options.streaming_result, bucket_idx);
        APSI_LOG_INFO("Received APSI query response");
      } catch (const exception &ex) {
        APSI_LOG_WARNING("Failed sending APSI query: " << ex.what());
        return -1;
      }

      int cnt = psi::apsi_wrapper::print_intersection_results(
          pair.second.second, items_vec, query_result, options.output_file,
          append_to_outfile);

      if (cnt > 0 && !append_to_outfile) {
        append_to_outfile = true;
      }

      total_matches += cnt;
    }

    if (match_cnt != nullptr) {
      *match_cnt = total_matches;
    }

    APSI_LOG_INFO("Total matches " << total_matches << " items.");

    print_transmitted_data(*channel);
    print_timing_report(::apsi::util::recv_stopwatch);

  } else {
    vector<::apsi::Item> items_vec(items.begin(), items.end());
    vector<::apsi::HashedItem> oprf_items;
    vector<::apsi::LabelKey> label_keys;
    try {
      APSI_LOG_INFO("Sending OPRF request for " << items_vec.size()
                                                << " items ");
      tie(oprf_items, label_keys) =
          psi::apsi_wrapper::Receiver::RequestOPRF(items_vec, *channel);
      APSI_LOG_INFO("Received OPRF response for " << items_vec.size()
                                                  << " items");
    } catch (const exception &ex) {
      APSI_LOG_WARNING("OPRF request failed: " << ex.what());
      return -1;
    }

    vector<::apsi::receiver::MatchRecord> query_result;
    try {
      APSI_LOG_INFO("Sending APSI query");
      query_result = receiver.request_query(oprf_items, label_keys, *channel,
                                            options.streaming_result);
      APSI_LOG_INFO("Received APSI query response");
    } catch (const exception &ex) {
      APSI_LOG_WARNING("Failed sending APSI query: " << ex.what());
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

int RunSender(const SenderOptions &options,
              const std::shared_ptr<yacl::link::Context> &lctx) {
  apsi::Log::SetConsoleDisabled(options.silent);
  apsi::Log::SetLogFile(options.log_file);
  apsi::Log::SetLogLevel(options.log_level);

  ::apsi::ThreadPoolMgr::SetThreadCount(options.threads);
  APSI_LOG_INFO("Setting thread count to "
                << ::apsi::ThreadPoolMgr::GetThreadCount());
  signal(SIGINT, sigint_handler);

  if (!options.experimental_enable_bucketize ||
      options.experimental_bucket_cnt == 0 ||
      options.experimental_bucket_folder.empty()) {
    // Check that the database file is valid
    psi::apsi_wrapper::throw_if_file_invalid(options.db_file);

    // Try loading first as a SenderDB, then as a CSV file
    shared_ptr<::apsi::sender::SenderDB> sender_db;
    ::apsi::oprf::OPRFKey oprf_key;
    if (!(sender_db = psi::apsi_wrapper::try_load_sender_db(
              options.db_file, options.params_file, oprf_key)) &&
        !(sender_db = psi::apsi_wrapper::try_load_csv_db(
              options.db_file, options.params_file, options.nonce_byte_count,
              options.compress, oprf_key))) {
      APSI_LOG_ERROR("Failed to create SenderDB: terminating");
      return -1;
    }

    // Print the total number of bin bundles and the largest number of bin
    // bundles for any bundle index
    uint32_t max_bin_bundles_per_bundle_idx = 0;
    for (uint32_t bundle_idx = 0;
         bundle_idx < sender_db->get_params().bundle_idx_count();
         bundle_idx++) {
      max_bin_bundles_per_bundle_idx = max(
          max_bin_bundles_per_bundle_idx,
          static_cast<uint32_t>(sender_db->get_bin_bundle_count(bundle_idx)));
    }
    APSI_LOG_INFO("SenderDB holds a total of "
                  << sender_db->get_bin_bundle_count() << " bin bundles across"
                  << sender_db->get_params().bundle_idx_count()
                  << " bundle indices");
    APSI_LOG_INFO("The largest bundle index holds "
                  << max_bin_bundles_per_bundle_idx << " bin bundles");

    // Try to save the SenderDB if a save file was given
    if (!options.sdb_out_file.empty() &&
        !psi::apsi_wrapper::try_save_sender_db(options.sdb_out_file, sender_db,
                                               oprf_key)) {
      return -1;
    }

    if (options.save_db_only) {
      APSI_LOG_INFO("Save db only. Exiting...");
      return 0;
    }

    // Run the dispatcher
    atomic<bool> stop = false;
    SenderDispatcher dispatcher(sender_db, oprf_key);

    if (options.channel == "zmq") {
      // The dispatcher will run until stopped.
      dispatcher.run(stop, options.zmq_port, options.streaming_result);
    } else {
      if (lctx) {
        lctx->ConnectToMesh();

        dispatcher.run(stop, lctx, options.streaming_result);
      } else {
        yacl::link::ContextDesc ctx_desc;
        ctx_desc.parties.push_back(
            {"sender", fmt::format("{}:{}", options.yacl_sender_ip_addr,
                                   options.yacl_sender_port)});
        ctx_desc.parties.push_back(
            {"receiver", fmt::format("{}:{}", options.yacl_receiver_ip_addr,
                                     options.yacl_receiver_port)});

        std::shared_ptr<yacl::link::Context> lctx_ =
            yacl::link::FactoryBrpc().CreateContext(ctx_desc, 0);

        lctx_->ConnectToMesh();

        dispatcher.run(stop, lctx_, options.streaming_result);
      }
    }
  } else {
    if (options.experimental_bucket_folder.empty()) {
      APSI_LOG_ERROR("experimental_bucket_folder is not provided.");
      return -1;
    }

    psi::apsi_wrapper::throw_if_directory_invalid(
        options.experimental_bucket_folder);

    // If experimental_bucket_folder is empty, then generate bucketized csv and
    // db files.

    shared_ptr<::apsi::sender::SenderDB> sender_db;
    ::apsi::oprf::OPRFKey oprf_key;

    if (std::filesystem::is_empty(options.experimental_bucket_folder)) {
      psi::apsi_wrapper::throw_if_file_invalid(options.db_file);

      CSVReader reader(options.db_file);
      reader.bucketize(options.experimental_bucket_cnt,
                       options.experimental_bucket_folder);

      MultiplexDiskCache disk_cache(options.experimental_bucket_folder, false);

      for (size_t i = 0; i < options.experimental_bucket_cnt; i++) {
        std::string db_path =
            GenerateDbPath(options.experimental_bucket_folder, i);
        sender_db = psi::apsi_wrapper::try_load_csv_db(
            disk_cache.GetPath(i), options.params_file,
            options.nonce_byte_count, options.compress, oprf_key);

        if (!sender_db) {
          APSI_LOG_ERROR("Failed to create SenderDB: " << db_path
                                                       << " terminating");
          return -1;
        }

        if (!psi::apsi_wrapper::try_save_sender_db(db_path, sender_db,
                                                   oprf_key)) {
          APSI_LOG_ERROR("Failed to save SenderDB: " << db_path
                                                     << " terminating");
          return -1;
        }
      }
    }

    if (options.save_db_only) {
      APSI_LOG_INFO("Save db only. Exiting...");
      return 0;
    }

    // Run the dispatcher
    atomic<bool> stop = false;
    std::shared_ptr<BucketSenderDbSwitcher> bucket_switcher =
        std::make_shared<BucketSenderDbSwitcher>(
            options.experimental_bucket_folder,
            options.experimental_bucket_cnt);
    SenderDispatcher dispatcher(bucket_switcher);

    if (options.channel == "zmq") {
      // The dispatcher will run until stopped.
      dispatcher.run(stop, options.zmq_port, options.streaming_result);
    } else {
      if (lctx) {
        lctx->ConnectToMesh();

        dispatcher.run(stop, lctx, options.streaming_result);
      } else {
        yacl::link::ContextDesc ctx_desc;
        ctx_desc.parties.push_back(
            {"sender", fmt::format("{}:{}", options.yacl_sender_ip_addr,
                                   options.yacl_sender_port)});
        ctx_desc.parties.push_back(
            {"receiver", fmt::format("{}:{}", options.yacl_receiver_ip_addr,
                                     options.yacl_receiver_port)});

        std::shared_ptr<yacl::link::Context> lctx_ =
            yacl::link::FactoryBrpc().CreateContext(ctx_desc, 0);

        lctx_->ConnectToMesh();

        dispatcher.run(stop, lctx_, options.streaming_result);
      }
    }
  }

  return 0;
}

}  // namespace psi::apsi_wrapper::cli
