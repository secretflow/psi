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

#include "generate_rand.h"

#include <vector>

#include "yacl/base/exception.h"

namespace pir::simple {
std::vector<uint64_t> generate_random_vector(size_t size,
                                             const uint64_t &modulus_,
                                             bool fast_mode) {
  YACL_ENFORCE(size > 0);
  YACL_ENFORCE(modulus_ > 1);

  std::vector<uint64_t> result(size);

  // Branch selection based on security requirements
  if (fast_mode) {
    // Non-cryptographic PRG path
    for (size_t i = 0; i < size; i++) {
      result[i] = yacl::crypto::FastRandU64() % modulus_;  // Unsafe modulo
    }
  } else {
    // Cryptographic PRG path (production use)
    for (size_t i = 0; i < size; i++) {
      result[i] = yacl::crypto::RandU64() % modulus_;  // Secure random
    }
  }
  return result;
}
}  // namespace pir::simple
