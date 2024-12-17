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

#include "yacl/base/int128.h"

#include "psi/algorithm/spiral/arith/arith.h"
#include "psi/algorithm/spiral/params.h"

namespace psi::spiral::arith {

inline std::uint64_t BarrettU64(const Params& params, std::uint64_t val) {
  return BarrettRawU64(val, params.BarrettCr1Modulus(), params.Modulus());
}

inline std::uint64_t BarrettCoeffU64(const Params& params, std::uint64_t val,
                                     std::size_t moduli_idx) {
  return BarrettRawU64(val, params.BarrettCr1(moduli_idx),
                       params.Moduli(moduli_idx));
}

inline std::uint64_t BarrettReductionU128(const Params& params, uint128_t val) {
  return BarrettReductionU128Raw(val, params.BarrettCr0Modulus(),
                                 params.BarrettCr1Modulus(), params.Modulus());
}

inline std::uint64_t MultiplyModular(const Params& params, std::uint64_t a,
                                     std::uint64_t b, std::size_t moduli_idx) {
  return BarrettCoeffU64(params, a * b, moduli_idx);
}

inline std::uint64_t MultiplyAddModular(const Params& params, std::uint64_t a,
                                        std::uint64_t b, std::uint64_t x,
                                        std::size_t moduli_idx) {
  return BarrettCoeffU64(params, a * b + x, moduli_idx);
}

inline std::uint64_t AddModular(const Params& params, std::uint64_t a,
                                std::uint64_t b, std::size_t moduli_idx) {
  return BarrettCoeffU64(params, a + b, moduli_idx);
}

inline std::uint64_t InvertModular(const Params& params, std::uint64_t a,
                                   std::size_t moduli_idx) {
  return params.Moduli(moduli_idx) - a;
}

inline std::uint64_t ModularReduce(const Params& params, std::uint64_t a,
                                   std::size_t moduli_idx) {
  return BarrettCoeffU64(params, a, moduli_idx);
}

}  // namespace psi::spiral::arith
