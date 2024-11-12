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
  uint64_t items_num = 1 << 16;
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

  std::vector<std::future<void>> futures;
  futures.emplace_back(std::async(std::launch::async, [&] {
    MocRr22VoleSender sender(seed2);
    sender.Send(lctxs[0], absl::MakeSpan(c));
    delta = sender.GetDelta();
    lctxs[0]->WaitLinkTaskFinish();
  }));
  futures.emplace_back(std::async(std::launch::async, [&] {
    MocRr22VoleReceiver receiver(seed2);
    receiver.Recv(lctxs[1], absl::MakeSpan(a), absl::MakeSpan(b));
    lctxs[1]->WaitLinkTaskFinish();
  }));
  for (auto& f : futures) {
    f.get();
  }

  okvs::Galois128 delta_gf128(delta);

  for (size_t i = 0; i < vole_num; ++i) {
    EXPECT_EQ(c[i] ^ b[i], (delta_gf128 * a[i]).get<uint128_t>(0));
  }
}

TEST_P(Rr22OprfTest, OprfTest) {
  auto params = GetParam();

  SPDLOG_INFO(
      "n: {}, mode: {}, malicious: {}", params.items_num,
      params.mode == Rr22PsiMode::LowCommMode ? "LowCommMode" : "FastMode",
      params.malicious);

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  lctxs[0]->SetRecvTimeout(30 * 6 * 1000);
  lctxs[1]->SetRecvTimeout(30 * 6 * 1000);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;
  std::vector<uint128_t> values(item_size);

  prng.Fill(absl::MakeSpan(values));

  Rr22OprfSender oprf_sender(kRr22OprfBinSize, kRr22DefaultSsp, params.mode);
  Rr22OprfReceiver oprf_receiver(kRr22OprfBinSize, kRr22DefaultSsp,
                                 params.mode);

  std::vector<uint128_t> oprf_a(item_size);
  std::vector<uint128_t> oprf_b(item_size);

  auto oprf_sender_proc = std::async([&] {
    oprf_sender.Init(lctxs[0], item_size);
    auto sender_inputs_hash = oprf_sender.Send(lctxs[0], values);

    oprf_a = oprf_sender.Eval(values, absl::MakeSpan(sender_inputs_hash));
    lctxs[0]->WaitLinkTaskFinish();
  });
  auto oprf_receiver_proc = std::async([&] {
    oprf_receiver.Init(lctxs[1], item_size, 1);
    oprf_b = oprf_receiver.Recv(lctxs[1], values);
    lctxs[1]->WaitLinkTaskFinish();
  });

  oprf_sender_proc.get();
  oprf_receiver_proc.get();

  EXPECT_EQ(oprf_a, oprf_b);
}

INSTANTIATE_TEST_SUITE_P(
    OprfTest_Instances, Rr22OprfTest,
    testing::Values(TestParams{1 << 12, Rr22PsiMode::LowCommMode},
                    TestParams{1 << 12, Rr22PsiMode::FastMode},
                    TestParams{1 << 12, Rr22PsiMode::FastMode, true}));

}  // namespace psi::rr22
