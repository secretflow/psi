// Copyright 2024 zhangwfjh
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

#include "experimental/psi/psi21/el_q_psi/el_q_psi.h"

#include <random>
#include <set>
#include <vector>

#include "gtest/gtest.h"
#include "psi/psi/utils/test_utils.h"
#include "yacl/link/test_util.h"

namespace psi::psi {

namespace {

struct NCTestParams {
  std::vector<size_t> item_size;
  size_t intersection_size;
  size_t n;
};

std::vector<std::vector<std::string>> CreateNPartyItems(
    const NCTestParams& params) {
  std::vector<std::vector<std::string>> ret(params.item_size.size() + 1);
  ret[params.item_size.size()] =
      test::CreateRangeItems(1, params.intersection_size);

  for (size_t idx = 0; idx < params.item_size.size(); ++idx) {
    ret[idx] =
        test::CreateRangeItems((idx + 1) * 1000000, params.item_size[idx]);
  }

  for (size_t idx = 0; idx < params.item_size.size(); ++idx) {
    std::set<size_t> idx_set;
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

    if (/*idx > dis(gen) &&*/ idx >= params.n) {
      break;
    }
  }
  return ret;
}

}  // namespace

class NCPsiTest : public testing::TestWithParam<NCTestParams> {};

// FIXME : this test is not stable in arm env
TEST_P(NCPsiTest, Works) {
  std::vector<std::vector<std::string>> items;
  std::vector<std::vector<std::string>> resultvec;
  std::vector<std::string> finalresult;

  auto params = GetParam();
  size_t n = params.n;
  items = CreateNPartyItems(params);
  size_t leader_rank = 0;
  uint128_t maxlength = 0;

  if (n >= params.item_size.size()) {
    SPDLOG_INFO("param error: n > items[0].size() ");
    return;
  }

  if (n <= 0) {
    SPDLOG_INFO("param error: n <= 0 ");
    return;
  }

  /*
  for (size_t j{}; j != items[0].size(); ++j) {
    SPDLOG_INFO(" items[{}][{}] = {}, size{}", 0, j, items[0][j],
                items[0].size());
  }
  */
  for (size_t i = 0; i < params.item_size.size() - 1; i++) {
    std::vector<std::vector<std::string>> items1;
    items1.push_back(items[0]);
    items1.push_back(items[i + 1]);
    leader_rank = 0;

    /*
    for (size_t j{}; j != items[i + 1].size(); ++j) {
         SPDLOG_INFO(" items[{}][{}] = {}, size{}", i+1, j, items[i+1][j],
         items[i+1].size());
    }
    */

    auto ctxs = yacl::link::test::SetupWorld(2);
    auto proc = [&](int idx) -> std::vector<std::string> {
      NcParty::Options opts;
      opts.link_ctx = ctxs[idx];
      opts.leader_rank = leader_rank;
      NcParty op(opts);

      return op.Run(items[idx]);
    };

    size_t world_size = ctxs.size();
    std::vector<std::future<std::vector<std::string>>> f_links(world_size);
    for (size_t j = 0; j < world_size; j++) {
      f_links[j] = std::async(proc, j);
    }
    sleep(1);

    std::vector<std::string> result;
    result = f_links[0].get();
    resultvec.push_back(result);

    /*
    for (size_t j = 0; j < result.size(); j++) {
        SPDLOG_INFO("i{}  j{}, result[j] {}  size{}", i, j, result[j],
                    result.size());
    }*/
  }

  maxlength = items[0].size();
  std::vector<uint128_t> qpsivector;
  for (size_t j = 0; j < maxlength; j++) {
    uint128_t sum = 0;
    for (size_t i = 0; i < params.item_size.size() - 1; i++) {
      // 如果有的集合没有那么多项就continue
      // results[i] = f_links[i].get();
      if (resultvec[i].size() <= j) {
        continue;
      }

      // SPDLOG_INFO(" result[{}][{}] = {}", i, j, resultvec[i][j]);
      auto it = resultvec[i].begin() + j;
      std::string element = *it;
      if (element == "1") {
        sum++;
      }
    }
    if (sum >= n) {
      // todo//推入对应input元素 之后再查输入变量从param中怎么取出推入
      qpsivector.push_back(1);
    } else {
      qpsivector.push_back(0);
    }
  }

  // std::vector<std::string> intersectionnparty;
  for (size_t k{}; k != items[0].size(); ++k) {
    if (qpsivector[k] == 1) {
      finalresult.push_back(items[0][k]);
    }
  }

  for (size_t i{}; i != finalresult.size(); ++i) {
    SPDLOG_INFO("intersectionnparty = {}", finalresult[i]);
  }

  std::vector<std::string> intersection = items[params.item_size.size()];
  std::sort(intersection.begin(), intersection.end());

  std::sort(finalresult.begin(), finalresult.end());
  EXPECT_EQ(finalresult.size(), intersection.size());
  EXPECT_EQ(finalresult, intersection);
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, NCPsiTest,
    testing::Values(NCTestParams{{0, 3}, 0, 1},                 //
                    NCTestParams{{3, 0}, 0, 1},                 //
                    NCTestParams{{0, 0}, 0, 1},                 //
                    NCTestParams{{4, 3}, 2, 1},                 //
                    NCTestParams{{20, 17, 14}, 10, 2},          //
                    NCTestParams{{20, 17, 14, 30}, 10, 3},      //
                    NCTestParams{{20, 17, 14, 30, 35}, 11, 4},  //
                    NCTestParams{{20, 17, 14, 30, 35}, 0, 4}));
// testing::Values(NCTestParams{{3, 0}, 0, 1}));
}  // namespace psi::psi
