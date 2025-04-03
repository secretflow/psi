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

#include "yacl/base/exception.h"

namespace pir::simple {
uint64_t fast_inner_product_modq(const std::vector<uint64_t> &row,
                                 const std::vector<uint64_t> &col,
                                 const uint64_t &q) {
  YACL_ENFORCE(row.size() == col.size());
  YACL_ENFORCE(q > 0);

  const size_t len = row.size();
  uint128_t result = 0;  // 128-bit accumulator for large intermediate sums

  for (size_t i = 0; i < len; i++) {
    // 128-bit multiplication to prevent overflow
    uint128_t product = static_cast<uint128_t>(row[i]) * col[i];

    product %= q;  // Reduce product modulo q
    result += product;

    // Final reduction ensures result ∈ [0, q)
    result %= q;
  }

  return static_cast<uint64_t>(result);
}
}  // namespace pir::simple
