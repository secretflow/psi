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

#include <cassert>
#include <cstdint>
#include <vector>

#include "seal/modulus.h"

namespace psi::spiral::arith {

using NttTables = std::vector<std::vector<std::vector<std::uint64_t>>>;

NttTables BuildNttTables(std::size_t poly_len,
                         const std::vector<std::uint64_t>& moduli);

NttTables BuildNttTables(std::size_t poly_len,
                         const std::vector<seal::Modulus>& moduli);

std::vector<std::uint64_t> ScalePowersU32(std::uint32_t modulus,
                                          std::size_t poly_len,
                                          const std::vector<std::uint64_t>& in);

std::vector<std::uint64_t> PowersOfPrimitiveRoot(std::uint64_t root,
                                                 std::uint64_t modulus,
                                                 std::size_t poly_len_log2);

}  // namespace psi::spiral::arith