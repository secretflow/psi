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

#include "psi/algorithm/ypir/aes_prng.h"

namespace psi::ypir::byhe {

AESCTR_PRNG::AESCTR_PRNG() {
  const uint8_t key[16] = {};
  aes128_key_expansion(key);
  ctr_lo_ = 0;
  ctr_hi_ = 0;
}

AESCTR_PRNG::AESCTR_PRNG(const uint8_t key[16], uint64_t seed) {
  aes128_key_expansion(key);
  ctr_lo_ = seed;
  ctr_hi_ = 0;
}

void AESCTR_PRNG::refresh(uint64_t seed) {
  ctr_lo_ = seed;
  ctr_hi_ = 0;
}

void AESCTR_PRNG::aes128_key_expansion(const uint8_t key[16]) {
  __m128i tmp1, tmp2;
  tmp1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
  round_keys_[0] = tmp1;

  AES_128_ASSIST(tmp1, tmp2, 0x01);
  round_keys_[1] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x02);
  round_keys_[2] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x04);
  round_keys_[3] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x08);
  round_keys_[4] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x10);
  round_keys_[5] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x20);
  round_keys_[6] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x40);
  round_keys_[7] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x80);
  round_keys_[8] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x1B);
  round_keys_[9] = tmp1;
  AES_128_ASSIST(tmp1, tmp2, 0x36);
  round_keys_[10] = tmp1;
}

__m128i AESCTR_PRNG::aes128_encrypt_block(__m128i block,
                                          const __m128i round_keys[11]) {
  block = _mm_xor_si128(block, round_keys[0]);
  for (int i = 1; i < 10; ++i) {
    block = _mm_aesenc_si128(block, round_keys[i]);
  }
  block = _mm_aesenclast_si128(block, round_keys[10]);
  return block;
}

__m128i AESCTR_PRNG::next_block() {
  __m128i ctr_block = _mm_set_epi64x(ctr_hi_, ctr_lo_);
  __m128i out = aes128_encrypt_block(ctr_block, round_keys_);
  if (++ctr_lo_ == 0) {
    ++ctr_hi_;
  }
  return out;
}

void AESCTR_PRNG::fill_bytes(uint8_t* dst, size_t len) {
  while (len >= 16) {
    __m128i b = next_block();
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), b);
    dst += 16;
    len -= 16;
  }
  if (len > 0) {
    __m128i b = next_block();
    alignas(16) uint8_t tmp[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(tmp), b);
    std::memcpy(dst, tmp, len);
  }
}

}  // namespace psi::ypir::byhe
