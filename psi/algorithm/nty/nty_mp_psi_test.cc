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

#include "psi/algorithm/nty/nty_mp_psi.h"

#include <random>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/utils/test_utils.h"

namespace psi::nty {

namespace {

struct NtyMPTestParams {
  std::vector<size_t> item_size;
  size_t intersection_size;
  size_t aided_server_rank = 0;
  size_t helper_rank = 1;
  size_t receiver_rank = 2;
  size_t num_threads = 1;
  bool broadcast_result = true;
};

std::vector<std::vector<uint128_t>> CreateNPartyItems(
    const NtyMPTestParams& params) {
  std::vector<std::vector<uint128_t>> ret(params.item_size.size() + 1);
  ret[params.item_size.size()] =
      test::CreateItemHashes(1, params.intersection_size);

  for (size_t idx = 0; idx < params.item_size.size(); ++idx) {
    ret[idx] =
        test::CreateItemHashes((idx + 1) * 100000000, params.item_size[idx]);
  }

  for (size_t idx = 0; idx < params.item_size.size(); ++idx) {
    std::unordered_set<size_t> idx_set;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, params.item_size[idx] - 1);

    while (idx_set.size() < params.intersection_size) {
      idx_set.insert(dis(gen));
    }
    size_t j = 0;
    for (const auto& iter : idx_set) {
      ret[idx][iter] = ret[params.item_size.size()][j++];
    }
  }
  return ret;
}

}  // namespace

class NtyMPsiTest : public testing::TestWithParam<NtyMPTestParams> {};

TEST_P(NtyMPsiTest, Works) {
  yacl::ElapsedTimer timer;
  std::vector<std::vector<uint128_t>> items;
  auto params = GetParam();
  SPDLOG_INFO("begin create test data");
  items = CreateNPartyItems(params);
  SPDLOG_INFO("end create test data");
  SPDLOG_DEBUG("create test data time is {} ms", timer.CountMs());

  timer.Restart();

  auto ctxs = yacl::link::test::SetupWorld(params.item_size.size());
  size_t num_threads = params.num_threads;
  size_t aided_server_rank = params.aided_server_rank;
  size_t helper_rank = params.helper_rank;
  size_t receiver_rank = params.receiver_rank;
  bool broadcast_result = params.broadcast_result;
  bool malicious = false;
  auto proc = [&](int idx) -> std::vector<uint128_t> {
    NtyMPsiOptions opts(num_threads, aided_server_rank, helper_rank,
                        receiver_rank, broadcast_result, malicious);
    opts.link_ctx = ctxs[idx];
    NtyMPsi mpsi(opts);
    return mpsi.Run(items[idx]);
  };

  size_t world_size = ctxs.size();
  std::vector<std::future<std::vector<uint128_t>>> f_links(world_size);
  for (size_t i = 0; i < world_size; i++) {
    f_links[i] = std::async(proc, i);
  }

  std::vector<uint128_t> intersection = items[params.item_size.size()];
  std::sort(intersection.begin(), intersection.end());

  std::vector<std::vector<uint128_t>> results(world_size);
  for (size_t i = 0; i < world_size; i++) {
    results[i] = f_links[i].get();
  }
  if (broadcast_result) {
    for (size_t i = 0; i < world_size; i++) {
      std::sort(results[i].begin(), results[i].end());
      EXPECT_EQ(results[i].size(), intersection.size());
      EXPECT_EQ(results[i], intersection);
    }
  } else {
    std::sort(results[receiver_rank].begin(), results[receiver_rank].end());
    EXPECT_EQ(results[receiver_rank].size(), intersection.size());
    EXPECT_EQ(results[receiver_rank], intersection);
  }
  SPDLOG_DEBUG("multiparty psi time is {} ms", timer.CountMs());
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, NtyMPsiTest,
    testing::Values(
        NtyMPTestParams{{1 << 12, 1 << 12, 1 << 12, 1 << 12}, 1 << 3, 1, 2, 3},
        NtyMPTestParams{{1 << 16, 1 << 16, 1 << 16, 1 << 16}, 1 << 16},
        NtyMPTestParams{
            {1 << 20, 1 << 20, 1 << 20, 1 << 20}, 1 << 20, 1, 2, 3, 8, false},
        NtyMPTestParams{{1 << 12, 1 << 12, 1 << 12}, 1 << 12},
        NtyMPTestParams{{1 << 16, 1 << 16, 1 << 16}, 1 << 16},
        NtyMPTestParams{{1 << 20, 1 << 20, 1 << 20}, 1 << 20}));
}  // namespace psi::nty
