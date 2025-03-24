// Copyright 2025 The secretflow authors.
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

#include "inner_product.h"

namespace pir::simple {
__uint128_t fast_inner_product_modq(const std::vector<__uint128_t> &row,
                                    const std::vector<__uint128_t> &col,
                                    const size_t &q) {
  if (row.size() != col.size()) {
    throw std::invalid_argument("Row and column sizes do not match");
  }
  const size_t len = row.size();
  const __uint128_t q128 = static_cast<__uint128_t>(q);
  __uint128_t result = 0;

  constexpr size_t UNROLL = 4;
  constexpr size_t MOD_FREQ = 32;

  size_t i = 0;
  __uint128_t buffer = 0;

  for (; i + UNROLL <= len; i += UNROLL) {
    for (size_t j = 0; j < UNROLL; j++) {
      const size_t idx = i + j;
      const __uint128_t term = (row[idx] % q128) * (col[idx] % q128);
      buffer += term;

      if (idx % MOD_FREQ == MOD_FREQ - 1) {
        result += buffer % q128;
        buffer = 0;
        result %= q128;
      }
    }
  }

  for (; i < len; i++) {
    buffer += (row[i] % q128) * (col[i] % q128);
  }

  result = (result + buffer) % q128;
  return result;
}
}  // namespace pir::simple

