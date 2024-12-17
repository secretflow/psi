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
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "seal/seal.h"
#include "seal/util/common.h"
#include "seal/util/uintarith.h"
#include "seal/util/uintarithsmallmod.h"
#include "yacl/base/exception.h"
#include "yacl/base/int128.h"
#include "yacl/math/gadget.h"

#include "psi/algorithm/spiral/common.h"

namespace psi::spiral::arith {

inline std::uint64_t Log2(std::uint64_t a) { return yacl::math::Log2Floor(a); }

inline std::uint64_t Log2Ceil(std::uint64_t a) {
  return yacl::math::Log2Ceil(a);
}

inline std::pair<std::uint64_t, std::uint64_t> GetBarrettCrs(
    std::uint64_t modulus) {
  // represent 2^{128}
  std::array<std::uint64_t, 3> numerator{0, 0, 1};
  std::array<std::uint64_t, 3> quotient{0, 0, 0};
  seal::util::divide_uint192_inplace(numerator.data(), modulus,
                                     quotient.data());
  // barrett redeuce precomputation
  return std::make_pair(quotient[0], quotient[1]);
}

inline std::pair<std::array<std::uint64_t, kMaxModuli>,
                 std::array<std::uint64_t, kMaxModuli>>
GetBarrett(const std::vector<std::uint64_t>& moduli) {
  std::array<std::uint64_t, kMaxModuli> cr0{0, 0, 0, 0};
  std::array<std::uint64_t, kMaxModuli> cr1{0, 0, 0, 0};

  for (std::size_t i = 0; i < moduli.size(); ++i) {
    std::pair<std::uint64_t, std::uint64_t> crs = GetBarrettCrs(moduli[i]);
    cr0[i] = crs.first;
    cr1[i] = crs.second;
  }
  return std::make_pair(cr0, cr1);
}

inline std::uint64_t ExponentitateUintMod(std::uint64_t operand,
                                          std::uint64_t exponent,
                                          std::uint64_t modulus) {
  seal::Modulus mod(modulus);
  return seal::util::exponentiate_uint_mod(operand, exponent, mod);
}

inline std::uint64_t ExponentitateUintMod(std::uint64_t operand,
                                          std::uint64_t exponent,
                                          const seal::Modulus& mod) {
  return seal::util::exponentiate_uint_mod(operand, exponent, mod);
}

inline std::uint64_t ReverseBits(std::uint64_t x, std::uint64_t bit_count) {
  if (bit_count == 0) {
    return 0;
  }
  return seal::util::reverse_bits<std::uint64_t>(x, bit_count);
}

inline std::uint64_t Div2UintMod(std::uint64_t operand, std::uint64_t modulus) {
  seal::Modulus mod(modulus);
  return seal::util::div2_uint_mod(operand, mod);
}

inline std::uint64_t Div2UintMod(std::uint64_t operand,
                                 const seal::Modulus& mod) {
  return seal::util::div2_uint_mod(operand, mod);
}

inline std::uint64_t Recenter(std::uint64_t val, std::uint64_t from_modulus,
                              std::uint64_t to_modulus) {
  YACL_ENFORCE(from_modulus >= to_modulus);

  auto from_modulus_i64 = static_cast<std::int64_t>(from_modulus);
  auto to_modulus_i64 = static_cast<std::int64_t>(to_modulus);
  auto a_val = static_cast<std::int64_t>(val);

  if (val >= from_modulus / 2) {
    a_val -= from_modulus_i64;
  }

  a_val = a_val + (from_modulus_i64 / to_modulus_i64) * to_modulus_i64 +
          2 * to_modulus_i64;
  a_val %= to_modulus_i64;

  return static_cast<std::uint64_t>(a_val);
}

inline std::uint64_t BarrettRawU64(std::uint64_t input,
                                   std::uint64_t const_ratio_1,
                                   std::uint64_t modulus) {
  std::uint64_t tmp = 0ULL;
  seal::util::multiply_uint64_hw64(input, const_ratio_1,
                                   reinterpret_cast<unsigned long long*>(&tmp));

  std::uint64_t res = input - (tmp * modulus);

  return res >= modulus ? res - modulus : res;
}

inline std::uint64_t BarrettRawU128(uint128_t val, std::uint64_t cr0,
                                    std::uint64_t cr1, std::uint64_t modulus) {
  auto [h64, l64] = yacl::DecomposeUInt128(val);

  std::uint64_t tmp1 = 0ULL;
  std::uint64_t tmp3 = 0ULL;
  // seal api need unsigned long long type
  unsigned long long carry = 0ULL;
  // std::array<std::uint64_t, 2> tmp2 = {0ULL, 0ULL};
  unsigned long long tmp2[2]{0ULL, 0ULL};
  // Round 1
  // (x0 * m0)_1 , 即 x0 * m0 的高 64 bits
  seal::util::multiply_uint64_hw64(l64, cr0, &carry);
  // tmp2 = [(x0 * m1)_0, (x0 * m1)_1]
  seal::util::multiply_uint64(l64, cr1, tmp2);

  tmp3 = tmp2[1] + seal::util::add_uint64(tmp2[0], carry, &tmp1);

  // Round2
  seal::util::multiply_uint64(h64, cr0, tmp2);
  carry = tmp2[1] + seal::util::add_uint64(tmp1, tmp2[0], &tmp1);
  // This is all we care about
  tmp1 = h64 * cr1 + tmp3 + carry;

  // reduction
  tmp3 = l64 - tmp1 * modulus;
  // this is a lazy result \in [0, 2*modulus)
  return tmp3;
}

inline std::uint64_t BarrettReductionU128Raw(uint128_t val, std::uint64_t cr0,
                                             std::uint64_t cr1,
                                             std::uint64_t modulus) {
  std::uint64_t reduced_val = BarrettRawU128(val, cr0, cr1, modulus);
  reduced_val -= (modulus) * static_cast<std::uint64_t>(reduced_val >= modulus);
  return reduced_val;
}

inline std::uint64_t RecenertMod(std::uint64_t val, std::uint64_t small_modulus,
                                 std::uint64_t large_modulus) {
  YACL_ENFORCE_LT(val, small_modulus);

  auto val_i64 = static_cast<std::int64_t>(val);
  auto small_modulus_i64 = static_cast<std::int64_t>(small_modulus);
  auto large_modulus_i64 = static_cast<std::int64_t>(large_modulus);

  if (val_i64 > (small_modulus_i64 / 2)) {
    val_i64 -= small_modulus_i64;
  }
  if (val_i64 < 0) {
    val_i64 += large_modulus_i64;
  }
  return static_cast<std::uint64_t>(val_i64);
}

inline std::uint64_t Rescale(std::uint64_t a, std::uint64_t in_mod,
                             std::uint64_t out_mod) {
  auto in_mod_i64 = static_cast<std::int64_t>(in_mod);
  int128_t in_mod_i128 = yacl::MakeInt128(0, in_mod);
  int128_t out_mod_i128 = yacl::MakeInt128(0, out_mod);

  auto in_val = static_cast<std::int64_t>(a % in_mod);
  if (in_val >= (in_mod_i64 / 2)) {
    in_val -= in_mod_i64;
  }
  std::int64_t sign = (in_val >= 0) ? 1 : -1;
  // int64_t can directly mul int128_t
  // do need to firstly convert to
  int128_t val = in_val * out_mod_i128;

  // val + int64_t = int128_t + int64_t, this is ok
  int128_t result = (val + sign * (in_mod_i64 / 2)) / in_mod_i128;

  // if the low-64 bit's type is int64_t, you must be carefully use MakeInt128
  int128_t tmp = yacl::MakeInt128(0, (in_mod / out_mod) * out_mod);
  result = (result + tmp + (2 * out_mod_i128)) % out_mod_i128;

  YACL_ENFORCE(result >= 0);

  result = (result + out_mod_i128) % out_mod_i128;
  auto last_result = yacl::DecomposeInt128(result).second;

  return last_result;
}

inline std::uint64_t MultiplyUintMod(std::uint64_t a, std::uint64_t b,
                                     std::uint64_t modulus) {
  seal::Modulus mod(modulus);
  return seal::util::multiply_uint_mod(a, b, mod);
}

inline std::uint64_t MultiplyUintMod(std::uint64_t a, std::uint64_t b,
                                     const seal::Modulus& mod) {
  return seal::util::multiply_uint_mod(a, b, mod);
}

inline std::uint64_t MultiplyUintMod(std::uint64_t a, std::uint64_t b,
                                     std::uint64_t modulus,
                                     uint64_t barrett_cr0,
                                     uint64_t barrett_cr1) {
  unsigned long long z[2] = {0ULL, 0ULL};
  seal::util::multiply_uint64(a, b, z);
  uint128_t z128 = yacl::MakeUint128(z[1], z[0]);
  return BarrettReductionU128Raw(z128, barrett_cr0, barrett_cr1, modulus);
}

inline size_t UintNum(size_t len, size_t uint_len) {
  return (len + uint_len - 1) / uint_len;
}

}  // namespace psi::spiral::arith
