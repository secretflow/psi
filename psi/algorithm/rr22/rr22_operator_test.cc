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

#include "psi/algorithm/rr22/rr22_operator.h"

#include <future>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/test_util.h"

#include "psi/utils/batch_provider_impl.h"
#include "psi/utils/index_store.h"

namespace psi::rr22 {

namespace {

std::tuple<std::vector<std::string>, std::vector<std::string>,
           std::vector<uint64_t>>
GenerateTestData(size_t item_size, double p = 0.1) {
  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  std::vector<uint128_t> tmp_a(item_size);
  std::vector<uint128_t> tmp_b(item_size);

  prng.Fill(absl::MakeSpan(tmp_a));
  prng.Fill(absl::MakeSpan(tmp_b));

  std::mt19937 std_rand(yacl::crypto::FastRandU64());
  std::bernoulli_distribution dist(p);

  std::vector<uint64_t> indices;
  std::vector<std::string> inputs_a(item_size);
  std::vector<std::string> inputs_b(item_size);
  for (size_t i = 0; i < item_size; ++i) {
    inputs_a[i] = fmt::format("{}", tmp_a[i]);
    inputs_b[i] = fmt::format("{}", tmp_b[i]);
    if (dist(std_rand)) {
      inputs_b[i] = inputs_a[i];
      indices.push_back(i);
    }
  }
  return std::make_tuple(inputs_a, inputs_b, indices);
}

struct TestParams {
  uint64_t items_num;

  Rr22PsiMode mode = Rr22PsiMode::FastMode;

  bool malicious = false;

  size_t target_rank = 0;

  bool broadcast = false;

  bool pipeline_mode = true;
};

}  // namespace

class Rr22OperatorTest : public testing::TestWithParam<TestParams> {};

TEST_P(Rr22OperatorTest, Work) {
  auto params = GetParam();

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;

  std::vector<std::string> inputs_a;
  std::vector<std::string> inputs_b;
  std::vector<uint64_t> indices;

  std::tie(inputs_a, inputs_b, indices) = GenerateTestData(item_size);

  auto input_store_a = std::make_shared<MemoryDataStore>(inputs_a);
  auto input_store_b = std::make_shared<MemoryDataStore>(inputs_b);
  auto output_store_a = std::make_shared<MemoryResultStore>();
  auto output_store_b = std::make_shared<MemoryResultStore>();

  Rr22PsiOptions psi_options(40, 0, true);
  psi_options.mode = params.mode;
  psi_options.malicious = params.malicious;

  Rr22Operator::Options opts_a{.rr22_opts = psi_options,
                               .receiver_rank = params.target_rank,
                               .lctx = lctxs[0],
                               .broadcast_result = params.broadcast,
                               .pipeline_mode = params.pipeline_mode};

  Rr22Operator::Options opts_b{.rr22_opts = psi_options,
                               .receiver_rank = params.target_rank,
                               .lctx = lctxs[1],
                               .broadcast_result = params.broadcast,
                               .pipeline_mode = params.pipeline_mode};

  Rr22Operator op_a(std::move(opts_a), input_store_a, output_store_a);
  Rr22Operator op_b(std::move(opts_b), input_store_b, output_store_b);

  auto proc = [](Rr22Operator* op) {
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

  if (params.target_rank == 0 || params.broadcast == true) {
    ASSERT_EQ(index_res_a.size(), indices.size());
    for (size_t i = 0; i != index_res_a.size(); ++i) {
      ASSERT_EQ(index_res_a[i].data, indices[i]);
    }
  } else {
    ASSERT_TRUE(index_res_a.empty());
  }
  if (params.target_rank == 1 || params.broadcast == true) {
    ASSERT_EQ(index_res_b.size(), indices.size());
    for (size_t i = 0; i != index_res_b.size(); ++i) {
      ASSERT_EQ(index_res_b[i].data, indices[i]);
    }
  } else {
    ASSERT_TRUE(index_res_b.empty());
  }
}

INSTANTIATE_TEST_SUITE_P(
    CorrectTest_Instances, Rr22OperatorTest,
    testing::Values(TestParams{1 << 10, Rr22PsiMode::FastMode},
                    TestParams{1 << 10, Rr22PsiMode::FastMode, true},
                    TestParams{1 << 10, Rr22PsiMode::LowCommMode},
                    TestParams{1 << 10, Rr22PsiMode::FastMode, false, 1},
                    TestParams{1 << 10, Rr22PsiMode::FastMode, false, 1, true},
                    TestParams{1 << 10, Rr22PsiMode::FastMode, false, 1, true,
                               false}));
}  // namespace psi::rr22
