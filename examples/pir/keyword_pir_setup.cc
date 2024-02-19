// Copyright 2023 Ant Group Co., Ltd.
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

// clang-format off
// To generate ecc oprf secret key, start terminals:
// > dd if=/dev/urandom of=secret_key.bin bs=32 count=1
// To run the example, start terminals:
// > bazel run //examples/pir:keyword_pir_setup -c opt -- -in_path ../../data/psi_server_data.csv -oprf_key_path secret_key.bin
// >     -key_columns id -label_columns label -data_per_query 1 -label_max_len 40
// >     -setup_path pir_setup_dir
// clang-format on

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "examples/pir/utils.h"
#include "spdlog/spdlog.h"
#include "yacl/link/test_util.h"

#include "psi/apsi/pir.h"
#include "psi/apsi/psi_params.h"
#include "psi/apsi/receiver.h"
#include "psi/apsi/sender.h"
#include "psi/ecdh//ecdh_oprf_selector.h"

#include "psi/proto/pir.pb.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

DEFINE_string(in_path, "data.csv", "[in] pir data in file path");

DEFINE_string(oprf_key_path, "oprf_key.bin",
              "[in] ecc oprf secretkey file path, 32bytes binary file");

DEFINE_int32(count_per_query, 256, "data count per query");

DEFINE_string(key_columns, "id", "key columns");

DEFINE_string(label_columns, "label", "label columns");

DEFINE_bool(compress, false, "compress seal he plaintext");

DEFINE_int32(bucket, 1000000, "bucket size of pir query");

DEFINE_int32(max_label_length, 288, "pad label data to max len");

DEFINE_string(setup_path, ".", "[out] output path for db setup data");

DEFINE_int32(max_items_per_bin, 0,
             "max items per bin, i.e. Interpolate polynomial max degree");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  SPDLOG_INFO("setup");

  std::vector<std::string> ids = absl::StrSplit(FLAGS_key_columns, ',');
  std::vector<std::string> labels = absl::StrSplit(FLAGS_label_columns, ',');

  SPDLOG_INFO("in_path: {}", FLAGS_in_path);
  SPDLOG_INFO("key columns: {}", FLAGS_key_columns);
  SPDLOG_INFO("label columns: {}", FLAGS_label_columns);

  psi::PirSetupConfig config;

  config.set_pir_protocol(psi::PirProtocol::KEYWORD_PIR_LABELED_PSI);
  config.set_store_type(psi::KvStoreType::LEVELDB_KV_STORE);
  config.set_input_path(FLAGS_in_path);

  config.mutable_key_columns()->Add(ids.begin(), ids.end());
  config.mutable_label_columns()->Add(labels.begin(), labels.end());

  config.set_num_per_query(FLAGS_count_per_query);
  config.set_label_max_len(FLAGS_max_label_length);
  config.set_oprf_key_path(FLAGS_oprf_key_path);
  config.set_setup_path(FLAGS_setup_path);
  config.set_compressed(FLAGS_compress);
  config.set_bucket_size(FLAGS_bucket);
  config.set_max_items_per_bin(FLAGS_max_items_per_bin);

  psi::PirResultReport report = psi::apsi::PirSetup(config);

  SPDLOG_INFO("data count:{}", report.data_count());

  return 0;
}
