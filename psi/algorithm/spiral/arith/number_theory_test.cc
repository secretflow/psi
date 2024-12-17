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

#include "psi/algorithm/spiral/arith/number_theory.h"

#include <vector>

#include "gtest/gtest.h"
#include "yacl/base/exception.h"

namespace psi::spiral::arith {

TEST(NumberTheoryTest, IsPrimitiveRoot) {
  std::uint64_t modulus{11};
  ASSERT_TRUE(arith::IsPrimitiveRoot(10, 2, modulus));
  ASSERT_FALSE(arith::IsPrimitiveRoot(9, 2, modulus));
  ASSERT_FALSE(arith::IsPrimitiveRoot(10, 4, modulus));
}

TEST(NumberTheoryTest, GetPrimitiveRoot) {
  std::uint64_t modulus{11};

  ASSERT_EQ(10, arith::GetPrimitiveRoot(2, modulus));
  // primitive root do not exist
  ASSERT_THROW(arith::GetPrimitiveRoot(3, modulus), yacl::EnforceNotMet);

  modulus = 29;
  ASSERT_EQ(28, arith::GetPrimitiveRoot(2, modulus));

  std::vector<std::uint64_t> corrects{12, 17};
  ASSERT_TRUE(std::find(corrects.begin(), corrects.end(),
                        arith::GetPrimitiveRoot(4, modulus)) != corrects.end());
}

TEST(NumberTheoryTest, GetMinimalPrimitiveRoot) {
  std::uint64_t modulus{11};
  ASSERT_EQ(10, arith::GetMinimalPrimitiveRoot(2, modulus));

  modulus = 29;
  ASSERT_EQ(28, arith::GetMinimalPrimitiveRoot(2, modulus));
  ASSERT_EQ(12, arith::GetMinimalPrimitiveRoot(4, modulus));

  modulus = 1234565441;
  ASSERT_EQ(1234565440ULL, arith::GetMinimalPrimitiveRoot(2, modulus));
  ASSERT_EQ(249725733ULL, arith::GetMinimalPrimitiveRoot(8, modulus));
}

TEST(NumberTheoryTest, InvertUintMod) {
  std::uint64_t modulus;
  std::uint64_t input;

  input = 1;
  modulus = 2;
  ASSERT_EQ(1, arith::InvertUintMod(input, modulus));

  input = 2;
  modulus = 2;
  ASSERT_THROW(arith::InvertUintMod(input, modulus), yacl::EnforceNotMet);

  input = 3;
  modulus = 2;
  ASSERT_EQ(1, arith::InvertUintMod(input, modulus));

  input = 0xFFFFFF;
  modulus = 2;
  ASSERT_EQ(1, arith::InvertUintMod(input, modulus));
}

}  // namespace psi::spiral::arith
