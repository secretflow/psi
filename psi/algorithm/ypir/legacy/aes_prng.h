// Copyright 2026 The secretflow authors.
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

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <immintrin.h>

namespace psi::ypir::ypir_internal {

#define AES_128_ASSIST(t1, t2, rc)                     \
  t2 = _mm_aeskeygenassist_si128(t1, rc);              \
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(3, 3, 3, 3)); \
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));       \
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));       \
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));       \
  t1 = _mm_xor_si128(t1, t2);

class AESCTR_PRNG {
 public:
  AESCTR_PRNG();
  AESCTR_PRNG(const uint8_t key[16], uint64_t seed = 0);

  void refresh(uint64_t seed);
  void fill_bytes(uint8_t* dst, size_t len);

 private:
  void aes128_key_expansion(const uint8_t key[16]);
  __m128i aes128_encrypt_block(__m128i block, const __m128i round_keys[11]);
  __m128i next_block();

  __m128i round_keys_[11];
  uint64_t ctr_lo_ = 0;
  uint64_t ctr_hi_ = 0;
};

}  // namespace psi::ypir::ypir_internal
