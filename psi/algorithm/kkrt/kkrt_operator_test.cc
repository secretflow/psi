// Copyright 2025 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License";
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

#include "psi/algorithm/kkrt/kkrt_operator.h"

#include <future>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/utils/batch_provider_impl.h"
#include "psi/utils/index_store.h"
#include "psi/utils/test_utils.h"

struct TestParams {
  std::vector<std::string> items_a;
  std::vector<std::string> items_b;
  size_t target_rank = 0;
  bool broadcast = false;
};

namespace psi::kkrt {

namespace {

std::vector<std::string> CreateRangeItems(size_t begin, size_t size) {
  std::vector<std::string> ret;
  for (size_t i = 0; i < size; i++) {
    ret.push_back(std::to_string(begin + i));
  }
  return ret;
}

}  // namespace

class KkrtOperatorTest : public testing::TestWithParam<TestParams> {};

TEST_P(KkrtOperatorTest, Works) {
  auto params = GetParam();
  const int kWorldSize = 2;
  auto contexts = yacl::link::test::SetupWorld(kWorldSize);

  auto input_store_a = std::make_shared<MemoryDataStore>(params.items_a);
  auto input_store_b = std::make_shared<MemoryDataStore>(params.items_b);
  auto output_store_a = std::make_shared<MemoryResultStore>();
  auto output_store_b = std::make_shared<MemoryResultStore>();

  KkrtOperator::Options opts_a;
  opts_a.link_ctx = contexts[0];
  opts_a.receiver_rank = params.target_rank;
  opts_a.broadcast_result = params.broadcast;

  KkrtOperator::Options opts_b;
  opts_b.link_ctx = contexts[1];
  opts_b.receiver_rank = params.target_rank;
  opts_b.broadcast_result = params.broadcast;

  KkrtOperator op_a(std::move(opts_a), input_store_a, output_store_a);
  KkrtOperator op_b(std::move(opts_b), input_store_b, output_store_b);

  auto proc = [](KkrtOperator* op) {
    op->Init();
    op->Run();
    op->End();
  };

  auto fa = std::async(proc, &op_a);
  auto fb = std::async(proc, &op_b);

  if (params.items_a.empty() || params.items_b.empty()) {
    EXPECT_THROW(fa.get(), ::yacl::EnforceNotMet);
    EXPECT_THROW(fb.get(), ::yacl::EnforceNotMet);
    return;
  }

  fa.get();
  fb.get();

  auto index_res_a = output_store_a->GetData();
  std::sort(index_res_a.begin(), index_res_a.end(),
            [](const auto& l, const auto& r) { return l.data < r.data; });

  auto index_res_b = output_store_b->GetData();
  std::sort(index_res_b.begin(), index_res_b.end(),
            [](const auto& l, const auto& r) { return l.data < r.data; });

  auto intersection = test::GetIntersection(params.items_a, params.items_b);
  if (params.target_rank == 0 || params.broadcast == true) {
    EXPECT_EQ(index_res_a.size(), intersection.size());
    for (size_t i = 0; i != index_res_a.size(); ++i) {
      EXPECT_EQ(params.items_a[index_res_a[i].data], intersection[i]);
    }
  } else {
    EXPECT_TRUE(index_res_a.empty());
  }
  if (params.target_rank == 1 || params.broadcast == true) {
    EXPECT_EQ(index_res_b.size(), intersection.size());
    for (size_t i = 0; i != index_res_b.size(); ++i) {
      EXPECT_EQ(params.items_b[index_res_b[i].data], intersection[i]);
    }
  } else {
    EXPECT_TRUE(index_res_b.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, KkrtOperatorTest,
    testing::Values(
        TestParams{{"a", "b"}, {"b", "c"}, 0},
        TestParams{{"a", "b"}, {"b", "c"}, 1},
        TestParams{{"a", "b"}, {"b", "c"}, 0, true},
        TestParams{{"a", "b"}, {"c", "d"}},
        // size not equal
        TestParams{{"a", "b", "c"}, {"c", "d"}},  //
        TestParams{{"a", "b"}, {"b", "c", "d"}},  //
        // exception cases
        TestParams{{}, {"a"}}, TestParams{{"a"}, {}},
        // less than one batch
        TestParams{CreateRangeItems(0, 1000), CreateRangeItems(1, 1000)},  //
        TestParams{CreateRangeItems(0, 1000), CreateRangeItems(1, 800)},   //
        TestParams{CreateRangeItems(0, 800), CreateRangeItems(1, 1000)},   //
        // exactly one batch
        TestParams{CreateRangeItems(0, 1024), CreateRangeItems(1, 1024)},  //
        // more than one batch
        TestParams{CreateRangeItems(0, 4095), CreateRangeItems(1, 4095)},  //
        //
        TestParams{{}, {}}  //
        ));

}  // namespace psi::kkrt
