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

#include <fstream>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "experiment/psi/threshold_ub_psi/client.h"
#include "experiment/psi/threshold_ub_psi/common.h"
#include "experiment/psi/threshold_ub_psi/server.h"
#include "yacl/link/test_util.h"

#include "psi/utils/random_str.h"
#include "psi/utils/test_utils.h"

#include "psi/proto/psi.pb.h"
#include "psi/proto/psi_v2.pb.h"

namespace {
static void BM_ThresholdEcdhUbPsi(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    auto ctxs = yacl::link::test::SetupWorld(2);

    uint32_t server_size = state.range(0);
    uint32_t client_size = state.range(1);
    uint32_t threshold = state.range(2);

    std::vector<std::string> items_server =
        psi::test::CreateRangeItems(0, server_size);
    std::vector<std::string> items_client =
        psi::test::CreateRangeItems(1, client_size);

    std::string uuid_str = psi::GetRandomString();
    std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                     uuid_str};
    std::filesystem::create_directories(tmp_folder);

    psi::v2::UbPsiConfig server_config;
    psi::v2::UbPsiConfig client_config;
    
    psi::ecdh::GenerateUbPsiConfig(tmp_folder, items_server, items_client,
                                   threshold, server_config, client_config);

    auto proc_server = [&](const psi::v2::UbPsiConfig &ub_psi_config,
                           const std::shared_ptr<yacl::link::Context> &lctx) {
      psi::ecdh::ThresholdEcdhUbPsiServer server(ub_psi_config, lctx);
      server.Run();
    };

    auto proc_client = [&](const psi::v2::UbPsiConfig &ub_psi_config,
                           const std::shared_ptr<yacl::link::Context> &lctx) {
      psi::ecdh::ThresholdEcdhUbPsiClient client(ub_psi_config, lctx);
      client.Run();
    };
    state.ResumeTiming();

    std::future<void> fa = std::async(proc_server, server_config, ctxs[0]);
    std::future<void> fb = std::async(proc_client, client_config, ctxs[1]);

    fa.get();
    fb.get();

    {
      std::error_code ec;
      std::filesystem::remove_all(tmp_folder, ec);
      if (ec.value() != 0) {
        std::cout << "can not remove temp file folder" << std::endl;
        SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                    tmp_folder.string(), ec.message());
      }
    }
  }
}
}  // namespace

BENCHMARK(BM_ThresholdEcdhUbPsi)
    ->Args({1000000, 10000, 1000})      // 100w-1w, threshold:1000
    ->Args({1000000, 10000, 10000})     // 100w-1w, threshold:10000
    ->Args({5000000, 100000, 10000})    // 500w-10w, threshold:10000
    ->Args({5000000, 100000, 100000});  // 500w-10w, threshold:100000
