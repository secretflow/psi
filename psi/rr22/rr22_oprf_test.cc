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

#include "psi/rr22/rr22_oprf.h"

#include <future>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/test_util.h"

#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22 {

namespace {

constexpr size_t kRr22OprfBinSize = 1 << 14;
constexpr size_t kRr22DefaultSsp = 40;

struct TestParams {
  uint64_t items_num = 16;

  Rr22PsiMode mode = Rr22PsiMode::FastMode;
  bool malicious = false;
};

}  // namespace

class Rr22OprfTest : public testing::TestWithParam<TestParams> {};

TEST(Rr22OprfTest, MocVoleTest) {
  uint128_t seed2 = yacl::MakeUint128(0, 1);

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  size_t vole_num = 100;

  std::vector<uint128_t> a(vole_num);
  std::vector<uint128_t> b(vole_num);
  std::vector<uint128_t> c(vole_num);
  uint128_t delta = 0;

  MocRr22VoleSender sender(seed2);

  MocRr22VoleReceiver receiver(seed2);

  sender.Send(lctxs[0], absl::MakeSpan(c));
  delta = sender.GetDelta();
  receiver.Recv(lctxs[1], absl::MakeSpan(a), absl::MakeSpan(b));

  okvs::Galois128 delta_gf128(delta);

  for (size_t i = 0; i < vole_num; ++i) {
    EXPECT_EQ(c[i] ^ b[i], (delta_gf128 * a[i]).get<uint128_t>(0));
  }
}

TEST(Rr22OprfTest, SilverVoleTest) {
  auto lctxs = yacl::link::test::SetupWorld(2);  // setup network

  const auto codetype = yacl::crypto::CodeType::Silver5;
  const uint64_t num = 10000;

  std::vector<uint128_t> a(num);
  std::vector<uint128_t> b(num);
  std::vector<uint128_t> c(num);
  uint128_t delta = 0;

  auto sender = std::async([&] {
    auto sv_sender = yacl::crypto::SilentVoleSender(codetype);
    sv_sender.Send(lctxs[0], absl::MakeSpan(c));
    delta = sv_sender.GetDelta();
  });

  auto receiver = std::async([&] {
    auto sv_receiver = yacl::crypto::SilentVoleReceiver(codetype);
    sv_receiver.Recv(lctxs[1], absl::MakeSpan(a), absl::MakeSpan(b));
  });

  sender.get();
  receiver.get();

  okvs::Galois128 delta_gf128(delta);

  SPDLOG_INFO("delta:{}", delta);
  SPDLOG_INFO("a[i]:{}, b[i]:{}, c[0]:{}", a[0], b[0], c[0]);
  SPDLOG_INFO("a[i]*delta ^ b[0]:{}",
              (delta_gf128 * a[0]).get<uint128_t>(0) ^ b[0]);

  for (uint64_t i = 0; i < num; ++i) {
    EXPECT_EQ((delta_gf128 * a[i]).get<uint128_t>(0) ^ b[i], c[i]);
  }
}

TEST_P(Rr22OprfTest, OprfTest) {
  auto params = GetParam();

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  lctxs[0]->SetRecvTimeout(30 * 6 * 1000);
  lctxs[1]->SetRecvTimeout(30 * 6 * 1000);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;
  std::vector<uint128_t> values_a(item_size);
  std::vector<uint128_t> values_b(item_size);

  prng.Fill(absl::MakeSpan(values_a));
  prng.Fill(absl::MakeSpan(values_b));

  Rr22OprfSender oprf_sender(kRr22OprfBinSize, kRr22DefaultSsp, params.mode);
  Rr22OprfReceiver oprf_receiver(kRr22OprfBinSize, kRr22DefaultSsp,
                                 params.mode);

  std::vector<uint128_t> oprf_a(item_size);
  std::vector<uint128_t> oprf_b(item_size);

  auto oprf_sender_proc = std::async([&] {
    std::vector<uint128_t> sender_inputs_hash(item_size);
    oprf_sender.Send(lctxs[0], item_size, absl::MakeSpan(values_b),
                     absl::MakeSpan(sender_inputs_hash), 1);

    oprf_sender.Eval(absl::MakeSpan(values_b), absl::MakeSpan(oprf_a));
  });
  auto oprf_receiver_proc = std::async([&] {
    oprf_receiver.Recv(lctxs[1], item_size, values_b, absl::MakeSpan(oprf_b),
                       1);
  });

  oprf_sender_proc.get();
  oprf_receiver_proc.get();

  for (size_t i = 0; i < item_size; ++i) {
    SPDLOG_INFO("{} {}", oprf_a[i], oprf_b[i]);
    EXPECT_EQ(oprf_a[i], oprf_b[i]);
  }

  EXPECT_EQ(oprf_a, oprf_b);
}

INSTANTIATE_TEST_SUITE_P(
    OprfTest_Instances, Rr22OprfTest,
    testing::Values(TestParams{15, Rr22PsiMode::FastMode},
                    TestParams{15, Rr22PsiMode::LowCommMode},
                    TestParams{15, Rr22PsiMode::FastMode, true}));

}  // namespace psi::rr22
