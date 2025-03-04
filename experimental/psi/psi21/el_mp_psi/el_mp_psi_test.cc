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

#include "experimental/psi/psi21/el_mp_psi/el_mp_psi.h"

#include <random>
#include <set>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/utils/test_utils.h"

namespace psi::psi {

namespace {

struct NMpTestParams {
  std::vector<size_t> item_size;
  size_t intersection_size;
};

std::vector<std::vector<std::string>> CreateNPartyItems(
    const NMpTestParams& params) {
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
  }
  return ret;
}

}  // namespace

class NMpPsiTest : public testing::TestWithParam<NMpTestParams> {};

// FIXME : this test is not stable in arm env
TEST_P(NMpPsiTest, Works) {
  std::vector<std::vector<std::string>> items;
  std::vector<std::vector<std::string>> resultvec;
  std::vector<std::string> finalresult;

  auto params = GetParam();
  items = CreateNPartyItems(params);
  size_t leader_rank = 0;
  uint128_t maxlength = 0;

  for (size_t i = 0; i < params.item_size.size() - 1; i++) {
    std::vector<std::vector<std::string>> items1;
    items1.push_back(items[0]);
    items1.push_back(items[i + 1]);
    leader_rank = 0;

    auto ctxs = yacl::link::test::SetupWorld(2);
    auto proc = [&](int idx) -> std::vector<std::string> {
      NmpParty::Options opts;
      opts.link_ctx = ctxs[idx];
      opts.leader_rank = leader_rank;
      NmpParty op(opts);
      // for (size_t j{}; j != items1[idx].size(); ++j) {
      //   SPDLOG_INFO(" items[{}][{}] = {}, size{}", idx, i, items[idx][i],
      //   items[idx].size());
      // }

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

    // for (size_t j = 0; j < result.size() ; j++) {
    //   SPDLOG_INFO("i{}  j{}, result[j] {}  size{}",i,j,result[j],
    //   result.size());
    // }
  }

  maxlength = items[0].size();
  for (size_t j = 0; j < maxlength; j++) {
    for (size_t i = 0; i < params.item_size.size() - 1; i++) {
      if (resultvec[i].size() <= j) {
        break;
      }
      if (resultvec[i][j] != "0") {
        break;
      }

      if (i == params.item_size.size() - 2) {
        finalresult.push_back(items[0][j]);
      }
    }
  }

  std::vector<std::string> intersection = items[params.item_size.size()];
  std::sort(intersection.begin(), intersection.end());

  std::sort(finalresult.begin(), finalresult.end());
  EXPECT_EQ(finalresult.size(), intersection.size());
  EXPECT_EQ(finalresult, intersection);
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, NMpPsiTest,
    testing::Values(NMpTestParams{{0, 3}, 0},                 //
                    NMpTestParams{{3, 0}, 0},                 //
                    NMpTestParams{{0, 0}, 0},                 //
                    NMpTestParams{{4, 3}, 2},                 //
                    NMpTestParams{{20, 17, 14}, 10},          //
                    NMpTestParams{{20, 17, 14, 30}, 10},      //
                    NMpTestParams{{20, 17, 14, 30, 35}, 11},  //
                    NMpTestParams{{20, 17, 14, 30, 35}, 0}));

}  // namespace psi::psi
