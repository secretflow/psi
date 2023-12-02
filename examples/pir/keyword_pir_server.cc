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

#include "examples/utils.h"
#include "spdlog/spdlog.h"
#include "yacl/link/test_util.h"

#include "psi/pir/pir.h"
#include "psi/psi/core/ecdh_oprf/ecdh_oprf_selector.h"
#include "psi/psi/core/labeled_psi/psi_params.h"
#include "psi/psi/core/labeled_psi/receiver.h"
#include "psi/psi/core/labeled_psi/sender.h"
#include "psi/psi/utils/serialize.h"
#include "psi/psi/utils/utils.h"

#include "psi/pir/pir.pb.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

DEFINE_string(oprf_key_path, "oprf_key.bin",
              "[in] ecc oprf secretkey file path, 32bytes binary file");

DEFINE_string(setup_path, ".", "[in] db setup data path");

namespace {

constexpr uint32_t kLinkRecvTimeout = 30 * 60 * 1000;

}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  SPDLOG_INFO("server");

  auto link_ctx = MakeLink();

  link_ctx->SetRecvTimeout(kLinkRecvTimeout);

  psi::pir::PirServerConfig config;

  config.set_pir_protocol(psi::pir::PirProtocol::KEYWORD_PIR_LABELED_PSI);
  config.set_store_type(psi::pir::KvStoreType::LEVELDB_KV_STORE);

  config.set_oprf_key_path(FLAGS_oprf_key_path);
  config.set_setup_path(FLAGS_setup_path);

  psi::pir::PirResultReport report = psi::pir::PirServer(link_ctx, config);

  SPDLOG_INFO("data count:{}", report.data_count());

  return 0;
}