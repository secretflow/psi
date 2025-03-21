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

#pragma once

#include "psi/utils/resource_manager.h"
#include "psi/wrapper/apsi/utils/sender_db.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
struct DkPirSenderOptions {
  std::size_t threads = 1;
  std::size_t nonce_byte_count = 16;
  bool compress = false;
  bool streaming_result = true;
  bool skip_count_check = false;
  CurveType curve_type;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::string params_file;
  std::string source_file;
  std::string value_sdb_out_file;
  std::string count_sdb_out_file;
  std::string secret_key_file;
  std::string result_file;

  std::string tmp_folder;

  std::string key;
  std::vector<std::string> labels;
};

struct DkPirReceiverOptions {
  std::size_t threads = 1;
  bool streaming_result = true;
  bool skip_count_check = false;
  CurveType curve_type;

  // "all", "debug", "info", "warning", "error", "off"
  std::string log_level = "all";
  std::string log_file;
  bool silent = false;

  std::string params_file;
  std::string query_file;
  std::string result_file;

  std::string tmp_folder;

  std::string key;
  std::vector<std::string> labels;
};

int SenderOffline(const DkPirSenderOptions &options);

int SenderOnline(const DkPirSenderOptions &options,
                 const std::shared_ptr<yacl::link::Context> &lctx);

int ReceiverOnline(const DkPirReceiverOptions &options,
                   const std::shared_ptr<yacl::link::Context> &lctx);

}  // namespace psi::dkpir