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

#include "seal/seal.h"
#include "seal/util/numth.h"
#include "yacl/base/exception.h"

#include "psi/algorithm/spiral/arith/arith.h"

namespace psi::spiral::arith {

inline bool IsPrimitiveRoot(std::uint64_t root, std::uint64_t degree,
                            std::uint64_t modulus) {
  if (root == 0) {
    return false;
  }
  return arith::ExponentitateUintMod(root, degree >> 1, modulus) == modulus - 1;
}

inline std::uint64_t GetPrimitiveRoot(std::uint64_t degree,
                                      std::uint64_t modulus) {
  YACL_ENFORCE(modulus > 1);
  YACL_ENFORCE(degree >= 2);

  std::uint64_t result = 0ULL;
  // todo: consider remove seal Modulus usage
  seal::Modulus mod(modulus);
  // return must be true
  YACL_ENFORCE(seal::util::try_primitive_root(degree, mod, result),
               "{} mod {} primitive root do not exits", degree, modulus);

  return result;
}

inline std::uint64_t GetPrimitiveRoot(std::uint64_t degree,
                                      const seal::Modulus& mod) {
  YACL_ENFORCE(mod.value() > 1);
  YACL_ENFORCE(degree >= 2);

  std::uint64_t result = 0ULL;
  // return must be true
  YACL_ENFORCE(seal::util::try_primitive_root(degree, mod, result),
               "{} mod {} primitive root do not exits", degree, mod.value());

  return result;
}

inline std::uint64_t GetMinimalPrimitiveRoot(std::uint64_t degree,
                                             std::uint64_t modulus) {
  std::uint64_t result{0};
  // todo: consider remove seal Modulus usage
  seal::Modulus mod(modulus);
  // return must be true
  YACL_ENFORCE(seal::util::try_minimal_primitive_root(degree, mod, result));
  return result;
}

inline std::uint64_t GetMinimalPrimitiveRoot(std::uint64_t degree,
                                             const seal::Modulus& mod) {
  std::uint64_t result{0};
  // return must be true
  YACL_ENFORCE(seal::util::try_minimal_primitive_root(degree, mod, result));
  return result;
}

inline std::uint64_t InvertUintMod(std::uint64_t value, std::uint64_t modulus) {
  YACL_ENFORCE(value > 0);
  seal::Modulus mod(modulus);
  std::uint64_t result{0};
  YACL_ENFORCE(seal::util::try_invert_uint_mod(value, mod, result));
  return result;
}

inline std::uint64_t InvertUintMod(std::uint64_t value,
                                   const seal::Modulus& mod) {
  YACL_ENFORCE(value > 0);
  std::uint64_t result{0};
  YACL_ENFORCE(seal::util::try_invert_uint_mod(value, mod, result));
  return result;
}

}  // namespace psi::spiral::arith
