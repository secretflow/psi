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

#include <cstddef>
#include <string>
#include <vector>

#include "yacl/link/context.h"

namespace psi::apsi_wrapper::cli {

struct ReceiverOptions {
  std::size_t threads = 0;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::string query_file;
  std::string output_file;
  std::string params_file;

  // "zmq", "yacl"
  std::string channel = "zmq";

  std::string zmq_ip_addr;
  int zmq_port;

  std::string yacl_sender_ip_addr;
  std::string yacl_receiver_ip_addr;
  int yacl_sender_port;
  int yacl_receiver_port;

  bool streaming_result = true;

  // experimental bucketize
  bool experimental_enable_bucketize = false;
  size_t experimental_bucket_cnt = 10;
  size_t query_batch_size = 1;
};

struct SenderOptions {
  std::size_t threads = 64;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::size_t nonce_byte_count = 16;
  bool compress = false;

  // "zmq", "yacl"
  std::string channel = "zmq";

  std::string zmq_ip_addr;
  int zmq_port;

  std::string yacl_sender_ip_addr;
  std::string yacl_receiver_ip_addr;
  int yacl_sender_port;
  int yacl_receiver_port;

  std::string db_file;
  std::string source_file;
  std::string params_file;
  std::string sdb_out_file;

  bool streaming_result = true;
  bool save_db_only = false;

  // experimental bucketize
  bool experimental_enable_bucketize = false;
  size_t experimental_bucket_cnt;
  std::string experimental_bucket_folder;
  int experimental_db_generating_process_num = 8;
  int experimental_bucket_group_cnt = 512;
};

int RunReceiver(const ReceiverOptions& options,
                const std::shared_ptr<yacl::link::Context>& lctx = nullptr,
                int* match_cnt = nullptr);

int RunSender(const SenderOptions& options,
              const std::shared_ptr<yacl::link::Context>& lctx = nullptr);

}  // namespace psi::apsi_wrapper::cli
