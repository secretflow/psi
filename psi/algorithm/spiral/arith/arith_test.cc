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

#include "psi/algorithm/spiral/arith/arith.h"

#include <cstddef>
#include <cstdint>
#include <random>

#include "gtest/gtest.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral::arith {

namespace {
constexpr std::size_t kMaxLoop = 1000;
}

TEST(ArithTest, MultiplyUintMod) {
  std::uint64_t mod{2};
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 0, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 1, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(1, 0, mod));
  ASSERT_EQ(1ULL, arith::MultiplyUintMod(1, 1, mod));

  auto [cr0, cr1] = arith::GetBarrettCrs(mod);
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 0, mod, cr0, cr1));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 1, mod, cr0, cr1));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(1, 0, mod, cr0, cr1));
  ASSERT_EQ(1ULL, arith::MultiplyUintMod(1, 1, mod, cr0, cr1));

  mod = 10;
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 0, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 1, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(1, 0, mod));
  ASSERT_EQ(1ULL, arith::MultiplyUintMod(1, 1, mod));
  ASSERT_EQ(9ULL, arith::MultiplyUintMod(7, 7, mod));
  ASSERT_EQ(2ULL, arith::MultiplyUintMod(6, 7, mod));
  ASSERT_EQ(2ULL, arith::MultiplyUintMod(7, 6, mod));

  mod = 2305843009211596801ULL;
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 0, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(0, 1, mod));
  ASSERT_EQ(0ULL, arith::MultiplyUintMod(1, 0, mod));
  ASSERT_EQ(1ULL, arith::MultiplyUintMod(1, 1, mod));
  ASSERT_EQ(576460752302899200ULL,
            arith::MultiplyUintMod(1152921504605798400ULL,
                                   1152921504605798401ULL, mod));
  ASSERT_EQ(576460752302899200ULL,
            arith::MultiplyUintMod(1152921504605798401ULL,
                                   1152921504605798400ULL, mod));
  ASSERT_EQ(1729382256908697601ULL,
            arith::MultiplyUintMod(1152921504605798401ULL,
                                   1152921504605798401ULL, mod));
  ASSERT_EQ(1ULL, arith::MultiplyUintMod(2305843009211596800ULL,
                                         2305843009211596800ULL, mod));

  auto [cr00, cr11] = arith::GetBarrettCrs(mod);
  ASSERT_EQ(576460752302899200ULL,
            arith::MultiplyUintMod(1152921504605798400ULL,
                                   1152921504605798401ULL, mod, cr00, cr11));
  ASSERT_EQ(576460752302899200ULL,
            arith::MultiplyUintMod(1152921504605798401ULL,
                                   1152921504605798400ULL, mod, cr00, cr11));
  ASSERT_EQ(1729382256908697601ULL,
            arith::MultiplyUintMod(1152921504605798401ULL,
                                   1152921504605798401ULL, mod, cr00, cr11));
  ASSERT_EQ(1ULL,
            arith::MultiplyUintMod(2305843009211596800ULL,
                                   2305843009211596800ULL, mod, cr00, cr11));
}

TEST(ArithTest, ReverseBits) {
  ASSERT_EQ(0ULL, arith::ReverseBits(0ULL, 0));
  ASSERT_EQ(0ULL, arith::ReverseBits(0ULL, 1));
  ASSERT_EQ(0ULL, arith::ReverseBits(0ULL, 32));
  ASSERT_EQ(0ULL, arith::ReverseBits(0ULL, 64));

  ASSERT_EQ(0ULL, arith::ReverseBits(1ULL, 0));
  ASSERT_EQ(1ULL, arith::ReverseBits(1ULL, 1));
  ASSERT_EQ(1ULL << 31, arith::ReverseBits(1ULL, 32));
  ASSERT_EQ(1ULL << 63, arith::ReverseBits(1ULL, 64));

  ASSERT_EQ(0ULL, arith::ReverseBits(1ULL << 31, 0));
  ASSERT_EQ(0ULL, arith::ReverseBits(1ULL << 31, 1));
  ASSERT_EQ(1ULL, arith::ReverseBits(1ULL << 31, 32));
  ASSERT_EQ(1ULL << 32, arith::ReverseBits(1ULL << 31, 64));

  ASSERT_EQ(0ULL, arith::ReverseBits(0xFFFFULL << 16, 0));
  ASSERT_EQ(0ULL, arith::ReverseBits(0xFFFFULL << 16, 1));
  ASSERT_EQ(0xFFFFULL, arith::ReverseBits(0xFFFFULL << 16, 32));
  ASSERT_EQ(0xFFFFULL << 32, arith::ReverseBits(0xFFFFULL << 16, 64));

  ASSERT_EQ(0ULL, arith::ReverseBits(0x0000FFFFFFFF0000ULL, 0));
  ASSERT_EQ(0ULL, arith::ReverseBits(0x0000FFFFFFFF0000ULL, 1));
  ASSERT_EQ(0xFFFFULL, arith::ReverseBits(0x0000FFFFFFFF0000ULL, 32));
  ASSERT_EQ(0x0000FFFFFFFF0000ULL,
            arith::ReverseBits(0x0000FFFFFFFF0000ULL, 64));

  ASSERT_EQ(0ULL, arith::ReverseBits(0xFFFF0000FFFF0000ULL, 0));
  ASSERT_EQ(0ULL, arith::ReverseBits(0xFFFF0000FFFF0000ULL, 1));
  ASSERT_EQ(0xFFFFULL, arith::ReverseBits(0xFFFF0000FFFF0000ULL, 32));
  ASSERT_EQ(0x0000FFFF0000FFFFULL,
            arith::ReverseBits(0xFFFF0000FFFF0000ULL, 64));
}

TEST(ArithTest, BarrettRawU64) {
  std::uint64_t mod{10};
  auto const_ratio = arith::GetBarrettCrs(mod);

  ASSERT_EQ(0, arith::BarrettRawU64(0, const_ratio.second, mod));
  ASSERT_EQ(1, arith::BarrettRawU64(1, const_ratio.second, mod));
  ASSERT_EQ(8, arith::BarrettRawU64(8, const_ratio.second, mod));
  ASSERT_EQ(7, arith::BarrettRawU64(1234567, const_ratio.second, mod));
  ASSERT_EQ(0, arith::BarrettRawU64(12345670, const_ratio.second, mod));

  mod = 66974689739603969ULL;
  std::uint64_t cr1 = 275ULL;

  std::random_device rd;
  std::mt19937 rng(rd());

  for (std::size_t i = 0; i < kMaxLoop; ++i) {
    std::uint64_t val = rng();
    ASSERT_EQ(val % mod, arith::BarrettRawU64(val, cr1, mod));
  }
}

TEST(ArithTest, Div2UintMod) { ASSERT_EQ(5, arith::Div2UintMod(3, 7)); }

TEST(ArithTest, GetBarrettCrs) {
  std::pair<std::uint64_t, std::uint64_t> expected =
      std::make_pair(16144578669088582089ULL, 68736257792ULL);
  ASSERT_EQ(expected, arith::GetBarrettCrs(268369921ULL));

  expected = std::make_pair(10966983149909726427ULL, 73916747789ULL);
  ASSERT_EQ(expected, arith::GetBarrettCrs(249561089ULL));

  expected = std::make_pair(7906011006380390721ULL, 275ULL);
  ASSERT_EQ(expected, arith::GetBarrettCrs(66974689739603969ULL));
}
TEST(ArithTest, BarrettReductionU128Raw) {
  std::uint64_t modulus = 66974689739603969ULL;
  uint128_t modulus_u128 = yacl::MakeUint128(0ULL, modulus);

  std::function<std::uint64_t(std::uint64_t)> exec = [](uint128_t val) {
    return BarrettReductionU128Raw(val, 7906011006380390721ULL, 275ULL,
                                   66974689739603969ULL);
  };

  ASSERT_EQ(0, exec(modulus_u128));
  ASSERT_EQ(1, exec(modulus_u128 + 1));
  ASSERT_EQ(5, exec((modulus_u128 * 7) + 5));

  std::random_device rd;
  std::mt19937 rng(rd());
  for (std::size_t i = 0; i < kMaxLoop; ++i) {
    std::uint64_t val = rng();
    uint128_t val_u128 = yacl::MakeUint128(0ULL, val);
    ASSERT_EQ(val % modulus, exec(val_u128));
  }
  // compare with seal::util::barrett_reduce_128
  modulus = 13131313131313ULL;
  auto const_ratio = GetBarrettCrs(modulus);
  seal::Modulus mod(modulus);

  ASSERT_EQ(const_ratio.first, mod.const_ratio()[0]);
  ASSERT_EQ(const_ratio.second, mod.const_ratio()[1]);

  uint128_t val = yacl::MakeUint128(0, 0);
  ASSERT_EQ(0, BarrettReductionU128Raw(val, const_ratio.first,
                                       const_ratio.second, modulus));

  val = yacl::MakeUint128(0, 1);
  ASSERT_EQ(1, BarrettReductionU128Raw(val, const_ratio.first,
                                       const_ratio.second, modulus));

  val = yacl::MakeUint128(456, 123);
  ASSERT_EQ(8722750765283ULL,
            BarrettReductionU128Raw(val, const_ratio.first, const_ratio.second,
                                    modulus));

  val = yacl::MakeUint128(79797979797979, 24242424242424);
  ASSERT_EQ(1010101010101ULL,
            BarrettReductionU128Raw(val, const_ratio.first, const_ratio.second,
                                    modulus));
}

TEST(ArithTest, Rescale) {
  ASSERT_EQ(4, Rescale(3, 17, 21));
  ASSERT_EQ(2, Rescale(3, 21, 17));
  ASSERT_EQ(1, Rescale(1, 17, 21));

  std::uint64_t in_mod = 0x7fffffd8001ULL;
  std::uint64_t out_mod = 0x7fffffc8001ULL;

  EXPECT_EQ(Rescale(2721421219ULL, in_mod, out_mod), 2721421199ULL);
  EXPECT_EQ(Rescale(2093223862ULL, in_mod, out_mod), 2093223846ULL);
  EXPECT_EQ(Rescale(3304378079ULL, in_mod, out_mod), 3304378054ULL);
  EXPECT_EQ(Rescale(3286543357ULL, in_mod, out_mod), 3286543333ULL);
  EXPECT_EQ(Rescale(1506336168ULL, in_mod, out_mod), 1506336157ULL);
  EXPECT_EQ(Rescale(3294507908ULL, in_mod, out_mod), 3294507883ULL);
  EXPECT_EQ(Rescale(3602954393ULL, in_mod, out_mod), 3602954366ULL);
  EXPECT_EQ(Rescale(3268316190ULL, in_mod, out_mod), 3268316166ULL);
  EXPECT_EQ(Rescale(3730398221ULL, in_mod, out_mod), 3730398193ULL);
  EXPECT_EQ(Rescale(3537330165ULL, in_mod, out_mod), 3537330139ULL);

  std::uint64_t modulus = 66974689739603969ULL;
  std::uint64_t pt_modulus = 256;
  std::uint64_t in = 34795444278750647ULL;
  EXPECT_EQ(133, Rescale(in, modulus, pt_modulus));
}

}  // namespace psi::spiral::arith
