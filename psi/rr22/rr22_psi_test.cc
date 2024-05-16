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

#include "psi/rr22/rr22_psi.h"

#include <random>
#include <tuple>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/test_util.h"

namespace psi::rr22 {

namespace {

std::tuple<std::vector<uint128_t>, std::vector<uint128_t>, std::vector<size_t>>
GenerateTestData(size_t item_size, double p = 0.5) {
  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  std::vector<uint128_t> inputs_a(item_size);
  std::vector<uint128_t> inputs_b(item_size);

  prng.Fill(absl::MakeSpan(inputs_a));
  prng.Fill(absl::MakeSpan(inputs_b));

  std::mt19937 std_rand(yacl::crypto::FastRandU64());
  std::bernoulli_distribution dist(p);

  std::vector<size_t> indices;
  for (size_t i = 0; i < item_size; ++i) {
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
};

}  // namespace

class Rr22PsiTest : public testing::TestWithParam<TestParams> {};

TEST_P(Rr22PsiTest, CorrectTest) {
  auto params = GetParam();

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;

  std::vector<uint128_t> inputs_a;
  std::vector<uint128_t> inputs_b;
  std::vector<size_t> indices;

  std::tie(inputs_a, inputs_b, indices) = GenerateTestData(item_size);

  Rr22PsiOptions psi_options(40, 0, true);

  psi_options.mode = params.mode;
  psi_options.malicious = params.malicious;

  auto psi_sender_proc = std::async(
      [&] { Rr22PsiSenderInternal(psi_options, lctxs[0], inputs_a); });
  auto psi_receiver_proc = std::async(
      [&] { return Rr22PsiReceiverInternal(psi_options, lctxs[1], inputs_b); });

  psi_sender_proc.get();
  std::vector<size_t> indices_psi = psi_receiver_proc.get();
  std::sort(indices_psi.begin(), indices_psi.end());

  SPDLOG_INFO("{}?={}", indices.size(), indices_psi.size());
  EXPECT_EQ(indices.size(), indices_psi.size());

#if 0
  for (size_t i = 0; i < indices.size(); ++i) {
    SPDLOG_INFO("i:{} index:{} a:{}, b:{}", i, indices_psi[i],
                inputs_a[indices_psi[i]], inputs_b[indices_psi[i]]);
  }
#endif

  EXPECT_EQ(indices_psi, indices);
}

TEST_P(Rr22PsiTest, CompressParamsFalseTest) {
  auto params = GetParam();

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;

  std::vector<uint128_t> inputs_a;
  std::vector<uint128_t> inputs_b;
  std::vector<size_t> indices;

  std::tie(inputs_a, inputs_b, indices) = GenerateTestData(item_size);

  Rr22PsiOptions psi_options(40, 0, false);

  psi_options.mode = params.mode;
  psi_options.malicious = params.malicious;

  auto psi_sender_proc = std::async(
      [&] { Rr22PsiSenderInternal(psi_options, lctxs[0], inputs_a); });
  auto psi_receiver_proc = std::async(
      [&] { return Rr22PsiReceiverInternal(psi_options, lctxs[1], inputs_b); });

  psi_sender_proc.get();
  std::vector<size_t> indices_psi = psi_receiver_proc.get();
  std::sort(indices_psi.begin(), indices_psi.end());

  SPDLOG_INFO("{}?={}", indices.size(), indices_psi.size());
  EXPECT_EQ(indices.size(), indices_psi.size());

#if 0
  for (size_t i = 0; i < indices.size(); ++i) {
    SPDLOG_INFO("i:{} index:{} a:{}, b:{}", i, indices_psi[i],
                inputs_a[indices_psi[i]], inputs_b[indices_psi[i]]);
  }
#endif

  EXPECT_EQ(indices_psi, indices);
}

INSTANTIATE_TEST_SUITE_P(
    CorrectTest_Instances, Rr22PsiTest,
    testing::Values(TestParams{35, Rr22PsiMode::FastMode},
                    TestParams{35, Rr22PsiMode::LowCommMode},
                    TestParams{35, Rr22PsiMode::FastMode, true}));

}  // namespace psi::rr22
