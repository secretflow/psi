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

#include "gflags/gflags.h"

#include "psi/wrapper/apsi/cli/common_utils.h"
#include "psi/wrapper/apsi/cli/entry.h"

using namespace std;

DEFINE_uint64(threads, 0, "Number of threads to use");
DEFINE_string(log_file, "/tmp/sender.log", "Log file path");
DEFINE_bool(silent, false, "Do not write output to console");
DEFINE_string(
    log_level, "info",
    "One of 'all', 'debug', 'info' (default), 'warning', 'error', 'off'");

DEFINE_string(
    db_file, "examples/pir/apsi/data/db.csv",
    "Path to a CSV file describing the sender's dataset (an item-label pair on "
    "each row) or a file containing a serialized SenderDB; the CLI will first "
    "attempt to load the data as a serialized SenderDB, and – upon failure – "
    "will proceed to attempt to read it as a CSV file");
DEFINE_string(
    params_file, "examples/pir/apsi/parameters/1M-256.json",
    "Path to a JSON file describing the parameters to be used by the sender");
DEFINE_string(sdb_out_file, "/tmp/sender.db",
              "Save the SenderDB in the given file.");

DEFINE_uint64(
    nonce_byte_count, 16,
    "Number of bytes used for the nonce in labeled mode (default is 16)");
DEFINE_bool(compress, false,
            "Whether to compress the SenderDB in memory; this will make the "
            "memory footprint smaller at the cost of increased computation");

DEFINE_string(channel, "zmq", "One of 'zmp' (default), 'yacl'");
DEFINE_string(zmq_ip_addr, "localhost",
              "IP address for a sender endpoint(default is localhost)");
DEFINE_uint64(zmq_port, 1212, "TCP port to connect to (default is 1212)");

DEFINE_string(yacl_sender_ip_addr, "localhost",
              "IP address for a sender endpoint");
DEFINE_uint64(yacl_sender_port, 1213,
              "TCP port to connect to (default is 1213)");
DEFINE_string(yacl_receiver_ip_addr, "localhost",
              "IP address for a receiver endpoint");
DEFINE_uint64(yacl_receiver_port, 1214,
              "TCP port to connect to (default is 1214)");
DEFINE_uint64(streaming_result, true, "Send ResultPart once ready.");

DEFINE_bool(experimental_enable_bucketize, false,
            "Whether to split data in buckets and Each bucket would be a "
            "seperate SenderDB.");
DEFINE_uint64(experimental_bucket_cnt, 0, "The number of bucket to fit data.");
DEFINE_string(experimental_bucket_folder, "",
              "Folder to save bucketized small csv files and db files.");

int main(int argc, char *argv[]) {
  psi::apsi_wrapper::cli::prepare_console();

  gflags::AllowCommandLineReparsing();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  psi::apsi_wrapper::cli::SenderOptions options;
  options.threads = FLAGS_threads;
  options.log_level = FLAGS_log_level;
  options.log_file = FLAGS_log_file;
  options.silent = FLAGS_silent;
  options.zmq_port = FLAGS_zmq_port;
  options.zmq_ip_addr = FLAGS_zmq_ip_addr;
  options.yacl_sender_ip_addr = FLAGS_yacl_sender_ip_addr;
  options.yacl_sender_port = FLAGS_yacl_sender_port;
  options.yacl_receiver_ip_addr = FLAGS_yacl_receiver_ip_addr;
  options.yacl_receiver_port = FLAGS_yacl_receiver_port;
  options.channel = FLAGS_channel;
  options.streaming_result = FLAGS_streaming_result;
  options.db_file = FLAGS_db_file;
  options.params_file = FLAGS_params_file;
  options.sdb_out_file = FLAGS_sdb_out_file;

  options.experimental_enable_bucketize = FLAGS_experimental_enable_bucketize;
  options.experimental_bucket_cnt = FLAGS_experimental_bucket_cnt;
  options.experimental_bucket_folder = FLAGS_experimental_bucket_folder;

  return psi::apsi_wrapper::cli::RunSender(options);
}
