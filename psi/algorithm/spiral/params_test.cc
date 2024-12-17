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

#include "psi/algorithm/spiral/params.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

#include "psi/algorithm/spiral/arith/ntt_table.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {

constexpr std::array<std::uint64_t, 2> kFastExpansionBarrettCr0{
    0xe00d10c30b5fa9c9, 0x98328d11bc1054db};
constexpr std::array<std::uint64_t, 2> kFastExpansionBarrettCr1{68736257792,
                                                                73916747789};

constexpr std::uint64_t kFastExpansionBarrettCr0Modulus{7906011006380390721};
constexpr std::uint64_t kFastExpansionBarrettCr1Modulus{275};
constexpr std::uint64_t kFastExpansionMod0InvMod1{26136460727815280};
constexpr std::uint64_t kFastExpansionMod1InvMod0{40838229011788690};
constexpr std::uint64_t kFastExpansionModulus{66974689739603969};
constexpr std::uint64_t kFastExpansionModulusLog2{56};

}  // namespace

TEST(ParamsTest, Correct) {
  auto params = util::GetFastExpansionTestingParam();

  EXPECT_EQ(11, params.PolyLenLog2());
  for (size_t i = 0; i < kFastExpansionBarrettCr0.size(); ++i) {
    EXPECT_EQ(kFastExpansionBarrettCr0[i], params.BarrettCr0(i));
    EXPECT_EQ(kFastExpansionBarrettCr1[i], params.BarrettCr1(i));
  }

  EXPECT_EQ(kFastExpansionBarrettCr0Modulus, params.BarrettCr0Modulus());
  EXPECT_EQ(kFastExpansionBarrettCr1Modulus, params.BarrettCr1Modulus());
  EXPECT_EQ(kFastExpansionMod0InvMod1, params.Mod0InvMod1());
  EXPECT_EQ(kFastExpansionMod1InvMod0, params.Mod1InvMod0());
  EXPECT_EQ(kFastExpansionModulus, params.Modulus());
  EXPECT_EQ(kFastExpansionModulusLog2, params.ModulusLog2());
}

TEST(ParamsTest, Id) {
  auto params = util::GetFastExpansionTestingParam();
  auto params2 = util::GetFastExpansionTestingParam();
  auto params3 = util::GetTestParam();

  ASSERT_TRUE(params == params2);
  ASSERT_TRUE(params != params3);

  EXPECT_EQ(params.Id(), params2.Id());
  EXPECT_NE(params.Id(), params3.Id());
}

}  // namespace psi::spiral
