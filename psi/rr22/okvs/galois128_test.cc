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

#include "psi/rr22/okvs/galois128.h"

#include <sstream>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "openssl/modes.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

namespace psi::rr22::okvs {

namespace {

struct TestParams {
  uint64_t seed1;
  uint64_t seed2;
};

}  // namespace

class GaloisTest : public testing::TestWithParam<TestParams> {};

TEST_P(GaloisTest, Works) {
  auto params = GetParam();

  Galois128 a(0, params.seed1);
  Galois128 b(0, params.seed2);

  Galois128 c = a * b;

  uint128_t a128 = yacl::MakeUint128(0, params.seed1);
  uint128_t b128 = yacl::MakeUint128(0, params.seed2);
  uint128_t z = cc_gf128Mul(a128, b128);

  EXPECT_EQ(std::memcmp(c.data(), &z, sizeof(uint128_t)), 0);

  uint64_t seed = yacl::crypto::FastRandU64();
  yacl::crypto::Prg<uint64_t> prg(seed);

  for (size_t i = 0; i < 100000; ++i) {
    uint64_t rh = prg();
    uint64_t rl = prg();
    uint64_t lh = prg();
    uint64_t ll = prg();
    Galois128 a(rh, rl), b(lh, ll);

    Galois128 c = a * b;

    uint128_t a128 = yacl::MakeUint128(rh, rl);
    uint128_t b128 = yacl::MakeUint128(lh, ll);
    uint128_t z = cc_gf128Mul(a128, b128);

    EXPECT_EQ(std::memcmp(c.data(), &z, sizeof(uint128_t)), 0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, GaloisTest,
    testing::Values(TestParams{1, 2}, TestParams{3, 2}, TestParams{3, 4},
                    TestParams{yacl::crypto::FastRandU64(),
                               yacl::crypto::FastRandU64()}));

}  // namespace psi::rr22::okvs
