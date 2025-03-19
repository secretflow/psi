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

#include "/root/pir/simple/include/generate_rand.h"
#include <limits>
#include <vector>

namespace pir::simple {

__uint128_t generate_128bit_random(std::random_device &rd) {
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis(
      0, std::numeric_limits<uint64_t>::max());
  __uint128_t value = dis(gen);
  value <<= 64;
  value |= dis(gen);
  return value;
}

std::vector<__uint128_t> generate_random_vector(size_t size,
                                                const size_t &modulus) {
  assert(modulus > 0);
  std::random_device rd;
  std::vector<__uint128_t> result(size);
  for (size_t i = 0; i < size; i++) {
    result[i] = generate_128bit_random(rd) % modulus;
  }
  return result;
}
}  // namespace pir::simple
