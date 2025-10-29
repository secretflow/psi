// Copyright 2025 Ant Group Co., Ltd.
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

#include "psi/algorithm/ecdh/ecdh_operator.h"

#include <future>
#include <iostream>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/algorithm/ecdh/ecdh_psi.h"
#include "psi/cryptor/cryptor_selector.h"
#include "psi/utils/batch_provider_impl.h"
#include "psi/utils/index_store.h"
#include "psi/utils/test_utils.h"

struct TestParams {
  std::vector<std::string> items_a;
  std::vector<std::string> items_b;
  size_t target_rank;
  size_t bin_size = 1;
  psi::CurveType curve_type = psi::CurveType::CURVE_25519;
};

namespace std {

std::ostream& operator<<(std::ostream& out, const TestParams& params) {
  out << "target_rank=" << params.target_rank;
  return out;
}

}  // namespace std

namespace psi::ecdh {

class EcdhOperatorTest : public testing::TestWithParam<TestParams> {};

TEST_P(EcdhOperatorTest, Works) {
  auto params = GetParam();
  auto ctxs = yacl::link::test::SetupWorld(2);

  auto input_store_a = std::make_shared<MemoryDataStore>(params.items_a);
  auto input_store_b = std::make_shared<MemoryDataStore>(params.items_b);
  auto output_store_a = std::make_shared<MemoryResultStore>();
  auto output_store_b = std::make_shared<MemoryResultStore>();

  EcdhPsiOptions opts_a;
  opts_a.link_ctx = ctxs[0];
  opts_a.target_rank = params.target_rank;
  opts_a.ecc_cryptor = CreateEccCryptor(params.curve_type);

  EcdhPsiOptions opts_b;
  opts_b.link_ctx = ctxs[1];
  opts_b.target_rank = params.target_rank;
  opts_b.ecc_cryptor = CreateEccCryptor(params.curve_type);

  EcdhOperator op_a(std::move(opts_a), input_store_a, output_store_a,
                    params.bin_size);
  EcdhOperator op_b(std::move(opts_b), input_store_b, output_store_b,
                    params.bin_size);

  auto proc = [](EcdhOperator* op) {
    op->Init();
    op->Run();
    op->End();
  };

  auto fa = std::async(proc, &op_a);
  auto fb = std::async(proc, &op_b);

  fa.get();
  fb.get();

  auto index_res_a = output_store_a->GetData();
  std::sort(index_res_a.begin(), index_res_a.end(),
            [](const auto& l, const auto& r) { return l.data < r.data; });

  auto index_res_b = output_store_b->GetData();
  std::sort(index_res_b.begin(), index_res_b.end(),
            [](const auto& l, const auto& r) { return l.data < r.data; });

  auto intersection = test::GetIntersection(params.items_a, params.items_b);
  if (params.target_rank == yacl::link::kAllRank || params.target_rank == 0) {
    EXPECT_EQ(index_res_a.size(), intersection.size());
    for (size_t i = 0; i != index_res_a.size(); ++i) {
      EXPECT_EQ(params.items_a[index_res_a[i].data], intersection[i]);
    }
  } else {
    EXPECT_TRUE(index_res_a.empty());
  }
  if (params.target_rank == yacl::link::kAllRank || params.target_rank == 1) {
    EXPECT_EQ(index_res_b.size(), intersection.size());
    for (size_t i = 0; i != index_res_b.size(); ++i) {
      EXPECT_EQ(params.items_b[index_res_b[i].data], intersection[i]);
    }
  } else {
    EXPECT_TRUE(index_res_b.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, EcdhOperatorTest,
    testing::Values(
        TestParams{{"a", "b"}, {"b", "c"}, yacl::link::kAllRank},  //
        TestParams{{"a", "b"}, {"b", "c"}, 0},                     //
        TestParams{{"a", "b"}, {"b", "c"}, 1},                     //
        //
        TestParams{{"a", "b"}, {"c", "d"}, yacl::link::kAllRank},  //
        TestParams{{"a", "b"}, {"c", "d"}, 0},                     //
        TestParams{{"a", "b"}, {"c", "d"}, 1},                     //
        //
        TestParams{{}, {"a"}, yacl::link::kAllRank},  //
        TestParams{{}, {"a"}, 0},                     //
        TestParams{{}, {"a"}, 1},                     //
        //
        TestParams{{"a"}, {}, yacl::link::kAllRank},  //
        TestParams{{"a"}, {}, 0},                     //
        TestParams{{"a"}, {}, 1},                     //
        TestParams{test::CreateRangeItems(0, 40961),
                   test::CreateRangeItems(5, 40961), yacl::link::kAllRank, 2},
        //
        TestParams{{}, {}, yacl::link::kAllRank},  //
        TestParams{{}, {}, 0},                     //
        TestParams{{}, {}, 1},                     //
        // test sm2
        TestParams{test::CreateRangeItems(0, 4096),
                   test::CreateRangeItems(1, 4095), yacl::link::kAllRank,
                   CurveType::CURVE_SM2},  //
        // exactly one batch
        TestParams{test::CreateRangeItems(0, 4096),
                   test::CreateRangeItems(1, 4096), yacl::link::kAllRank,
                   CurveType::CURVE_SECP256K1},  //
        // more than one batch
        TestParams{test::CreateRangeItems(0, 4096),
                   test::CreateRangeItems(1, 4096), yacl::link::kAllRank,
                   CurveType::CURVE_FOURQ}  //
        ));

}  // namespace psi::ecdh
