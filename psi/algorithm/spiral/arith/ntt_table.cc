

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

#include "psi/algorithm/spiral/arith/ntt_table.h"

#include <utility>

#include "seal/modulus.h"
#include "yacl/base/exception.h"

#include "psi/algorithm/spiral/arith/number_theory.h"

namespace psi::spiral::arith {

std::vector<std::uint64_t> ScalePowersU32(
    std::uint32_t modulus, std::size_t poly_len,
    const std::vector<std::uint64_t>& in) {
  std::vector<std::uint64_t> scaled_powers(poly_len, 0ULL);

  for (std::size_t i = 0; i < poly_len; ++i) {
    std::uint64_t wide_val = in[i] << 32;
    std::uint64_t quotient = wide_val / static_cast<std::uint64_t>(modulus);
    scaled_powers[i] =
        static_cast<std::uint64_t>((static_cast<std::uint32_t>(quotient)));
  }

  return scaled_powers;
}

std::vector<std::uint64_t> PowersOfPrimitiveRoot(std::uint64_t root,
                                                 std::uint64_t modulus,
                                                 std::size_t poly_len_log2) {
  std::size_t poly_len = 1 << poly_len_log2;
  std::vector<std::uint64_t> root_powers(poly_len, 0ULL);
  std::uint64_t power = root;

  root_powers[0] = 1;
  seal::Modulus mod(modulus);
  for (std::size_t i = 1; i < poly_len; ++i) {
    auto idx = arith::ReverseBits(i, poly_len_log2);
    root_powers[idx] = power;
    power = arith::MultiplyUintMod(power, root, mod);
  }
  return root_powers;
}

std::vector<std::uint64_t> PowersOfPrimitiveRoot(std::uint64_t root,
                                                 const seal::Modulus& mod,
                                                 std::size_t poly_len_log2) {
  std::size_t poly_len = 1 << poly_len_log2;
  std::vector<std::uint64_t> root_powers(poly_len, 0ULL);
  std::uint64_t power = root;

  root_powers[0] = 1;
  for (std::size_t i = 1; i < poly_len; ++i) {
    auto idx = arith::ReverseBits(i, poly_len_log2);
    root_powers[idx] = power;
    power = arith::MultiplyUintMod(power, root, mod);
  }
  return root_powers;
}

NttTables BuildNttTables(std::size_t poly_len,
                         const std::vector<std::uint64_t>& moduli) {
  YACL_ENFORCE(poly_len > 0);
  YACL_ENFORCE(moduli.size() > 0);

  std::size_t poly_len_log2 = arith::Log2(poly_len);

  NttTables tables;

  for (std::size_t i = 0; i < moduli.size(); ++i) {
    std::uint64_t modulus = moduli[i];
    seal::Modulus mod(modulus);
    // todo: why need convert? maybe reduce error?
    auto modulus_u32 = static_cast<std::uint32_t>(modulus);

    std::uint64_t root = arith::GetMinimalPrimitiveRoot(2 * poly_len, mod);
    std::uint64_t inv_root = arith::InvertUintMod(root, mod);

    auto root_powers = PowersOfPrimitiveRoot(root, mod, poly_len_log2);

    auto scaled_root_powers =
        ScalePowersU32(modulus_u32, poly_len, root_powers);

    auto inv_root_power = PowersOfPrimitiveRoot(inv_root, mod, poly_len_log2);

    for (std::size_t j = 0; j < poly_len; ++j) {
      inv_root_power[j] = arith::Div2UintMod(inv_root_power[j], mod);
    }

    auto scaled_inv_root_powers =
        ScalePowersU32(modulus_u32, poly_len, inv_root_power);

    std::vector<std::vector<std::uint64_t>> temp{
        std::move(root_powers), std::move(scaled_root_powers),
        std::move(inv_root_power), std::move(scaled_inv_root_powers)};

    tables.emplace_back(std::move(temp));
  }

  return tables;
}

NttTables BuildNttTables(std::size_t poly_len,
                         const std::vector<seal::Modulus>& moduli) {
  YACL_ENFORCE(poly_len > 0);
  YACL_ENFORCE(moduli.size() > 0);

  std::size_t poly_len_log2 = arith::Log2(poly_len);

  NttTables tables;

  for (std::size_t i = 0; i < moduli.size(); ++i) {
    std::uint64_t modulus = moduli[i].value();
    // todo: why need convert? maybe reduce error?
    auto modulus_u32 = static_cast<std::uint32_t>(modulus);

    std::uint64_t root =
        arith::GetMinimalPrimitiveRoot(2 * poly_len, moduli[i]);
    std::uint64_t inv_root = arith::InvertUintMod(root, moduli[i]);

    auto root_powers = PowersOfPrimitiveRoot(root, moduli[i], poly_len_log2);

    auto scaled_root_powers =
        ScalePowersU32(modulus_u32, poly_len, root_powers);

    auto inv_root_power =
        PowersOfPrimitiveRoot(inv_root, moduli[i], poly_len_log2);

    for (std::size_t j = 0; j < poly_len; ++j) {
      inv_root_power[j] = arith::Div2UintMod(inv_root_power[j], moduli[i]);
    }

    auto scaled_inv_root_powers =
        ScalePowersU32(modulus_u32, poly_len, inv_root_power);

    std::vector<std::vector<std::uint64_t>> temp{
        std::move(root_powers), std::move(scaled_root_powers),
        std::move(inv_root_power), std::move(scaled_inv_root_powers)};

    tables.emplace_back(std::move(temp));
  }

  return tables;
}

}  // namespace psi::spiral::arith
