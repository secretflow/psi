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
// To run the example, start terminals:
// > bazel run //examples/pir:keyword_pir_server -c opt -- -rank 0 -setup_path pir_setup_dir
// >        -oprf_key_path secret_key.bin
// clang-format on

#include <chrono>
#include <filesystem>
#include <string>

#include "examples/pir/utils.h"
#include "spdlog/spdlog.h"
#include "yacl/link/test_util.h"

#include "psi/pir/pir.h"
#include "psi/psi/core/ecdh_oprf/ecdh_oprf_selector.h"
#include "psi/psi/core/labeled_psi/psi_params.h"
#include "psi/psi/core/labeled_psi/receiver.h"
#include "psi/psi/core/labeled_psi/sender.h"
#include "psi/psi/utils/serialize.h"

#include "psi/proto/pir.pb.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

DEFINE_string(in_path, "data.csv", "[in] pir data in file path");

DEFINE_int32(count_per_query, 256, "data count per query");

DEFINE_string(key_columns, "id", "key columns");

DEFINE_string(label_columns, "label", "label columns");

DEFINE_int32(max_label_length, 288, "pad label data to max len");

DEFINE_bool(compress, false, "compress seal he plaintext");

DEFINE_int32(bucket, 1000000, "bucket size of pir query");

DEFINE_int32(max_items_per_bin, 0,
             "max items per bin, i.e. Interpolate polynomial max degree ");

namespace {

constexpr uint32_t kLinkRecvTimeout = 30 * 60 * 1000;

}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  SPDLOG_INFO("setup");

  auto link_ctx = MakeLink();

  link_ctx->SetRecvTimeout(kLinkRecvTimeout);

  std::vector<std::string> ids = absl::StrSplit(FLAGS_key_columns, ',');
  std::vector<std::string> labels = absl::StrSplit(FLAGS_label_columns, ',');

  psi::pir::PirSetupConfig config;

  config.set_pir_protocol(psi::pir::PirProtocol::KEYWORD_PIR_LABELED_PSI);
  config.set_store_type(psi::pir::KvStoreType::LEVELDB_KV_STORE);
  config.set_input_path(FLAGS_in_path);

  config.mutable_key_columns()->Add(ids.begin(), ids.end());
  config.mutable_label_columns()->Add(labels.begin(), labels.end());

  config.set_num_per_query(FLAGS_count_per_query);
  config.set_label_max_len(FLAGS_max_label_length);
  config.set_oprf_key_path("");
  config.set_setup_path("::memory");
  config.set_compressed(FLAGS_compress);
  config.set_bucket_size(FLAGS_bucket);
  config.set_max_items_per_bin(FLAGS_max_items_per_bin);

  psi::pir::PirResultReport report =
      psi::pir::PirMemoryServer(link_ctx, config);

  SPDLOG_INFO("data count:{}", report.data_count());

  return 0;
}
