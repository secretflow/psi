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
// > bazel run //examples/pir:keyword_pir_client -c opt -- -rank 1 -in_path ../../data/psi_client_data.csv.csv
// >       -key_columns id -out_path pir_out.csv
// clang-format on

#include <chrono>
#include <filesystem>
#include <string>

#include "examples/pir/utils.h"
#include "yacl/io/rw/csv_writer.h"

#include "psi/pir/pir.h"
#include "psi/psi/core/labeled_psi/psi_params.h"
#include "psi/psi/core/labeled_psi/receiver.h"
#include "psi/psi/utils/batch_provider.h"
#include "psi/psi/utils/serialize.h"

#include "psi/proto/pir.pb.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

DEFINE_string(in_path, "data.csv", "pir data in file path");

DEFINE_string(key_columns, "id", "key columns");

DEFINE_string(out_path, ".", "[out] pir query output path for db setup data");

namespace {

constexpr uint32_t kLinkRecvTimeout = 30 * 60 * 1000;

}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto link_ctx = MakeLink();

  link_ctx->SetRecvTimeout(kLinkRecvTimeout);

  std::vector<std::string> ids = absl::StrSplit(FLAGS_key_columns, ',');

  psi::pir::PirClientConfig config;

  config.set_pir_protocol(psi::pir::PirProtocol::KEYWORD_PIR_LABELED_PSI);

  config.set_input_path(FLAGS_in_path);
  config.mutable_key_columns()->Add(ids.begin(), ids.end());
  config.set_output_path(FLAGS_out_path);

  psi::pir::PirResultReport report = psi::pir::PirClient(link_ctx, config);

  SPDLOG_INFO("data count:{}", report.data_count());

  return 0;
}
