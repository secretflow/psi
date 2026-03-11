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

#include "psi/algorithm/spiral/arith/ntt.h"

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/types/span.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/spiral/arith/number_theory.h"

namespace psi::spiral::arith {

#ifndef __AVX2__

void NttForward(const Params& params, absl::Span<uint64_t> operand_overall) {
  std::size_t log_n = params.PolyLenLog2();
  std::size_t n = static_cast<size_t>(1) << log_n;

  for (std::size_t coeff_mod = 0; coeff_mod < params.CrtCount(); ++coeff_mod) {
    auto operand = operand_overall.subspan(coeff_mod * n, n);

    const auto& forward_table = params.GetNttForwardTable(coeff_mod);
    const auto& forward_table_prime = params.GetNttForwardPrimeTable(coeff_mod);
    // why need to convert uint32
    auto modulus_small = static_cast<std::uint32_t>(params.Moduli(coeff_mod));
    std::uint32_t two_times_modulus_small = 2 * modulus_small;

    for (std::size_t mm = 0; mm < log_n; ++mm) {
      std::size_t m = 1 << mm;
      std::size_t t = n >> (mm + 1);

      for (std::size_t i = 0; i < m; ++i) {
        uint64_t w = forward_table[m + i];
        uint64_t w_prime = forward_table_prime[m + i];

        auto op = operand.subspan(i * (2 * t), 2 * t);

        for (std::size_t j = 0; j < t; ++j) {
          std::uint32_t x = static_cast<std::uint32_t>(op[j]);
          std::uint32_t y = static_cast<std::uint32_t>(op[t + j]);

          std::uint32_t curr_x =
              x - (two_times_modulus_small *
                   static_cast<std::uint32_t>(x >= two_times_modulus_small));
          std::uint64_t q_tmp = (static_cast<std::uint64_t>(y) *
                                 static_cast<std::uint64_t>(w_prime)) >>
                                32;
          std::uint64_t q_new =
              w * static_cast<std::uint64_t>(y) -
              q_tmp * static_cast<std::uint64_t>(modulus_small);

          op[j] = curr_x + q_new;
          op[t + j] =
              curr_x + (static_cast<uint64_t>(two_times_modulus_small) - q_new);
        }
      }
    }
    // Update the operand with modulus constraints
    for (std::size_t i = 0; i < n; ++i) {
      operand[i] -=
          static_cast<std::uint64_t>(operand[i] >= two_times_modulus_small) *
          two_times_modulus_small;
      operand[i] -= static_cast<std::uint64_t>(operand[i] >= modulus_small) *
                    modulus_small;
    }
  }
}

// AVX2 version of ntt_forward
#else

void NttForward(const Params& params, absl::Span<uint64_t> operand_overall) {
  SPDLOG_DEBUG("using AVX2 NttForward");

  std::size_t log_n = params.PolyLenLog2();
  std::size_t n = static_cast<size_t>(1) << log_n;

  YACL_ENFORCE(n >= 4, "PolyLen must be >= 4 for AVX2 impl");
  YACL_ENFORCE(operand_overall.size() >= params.CrtCount() * n);

  for (std::size_t coeff_mod = 0; coeff_mod < params.CrtCount(); ++coeff_mod) {
    auto operand = operand_overall.subspan(coeff_mod * n, n);

    const auto& forward_table = params.GetNttForwardTable(coeff_mod);
    const auto& forward_table_prime = params.GetNttForwardPrimeTable(coeff_mod);
    auto modulus_small = static_cast<std::uint32_t>(params.Moduli(coeff_mod));
    std::uint32_t two_times_modulus_small = 2 * modulus_small;

    for (std::size_t mm = 0; mm < log_n; ++mm) {
      std::size_t m = 1 << mm;
      std::size_t t = n >> (mm + 1);

      for (std::size_t i = 0; i < m; ++i) {
        uint64_t w = forward_table[m + i];
        uint64_t w_prime = forward_table_prime[m + i];
        auto op = operand.subspan(i * (2 * t), 2 * t);

        if (t < 4) {
          for (std::size_t j = 0; j < t; ++j) {
            uint32_t x = static_cast<uint32_t>(op[j]);
            uint32_t y = static_cast<uint32_t>(op[t + j]);

            std::uint32_t curr_x =
                x - (two_times_modulus_small *
                     static_cast<std::uint32_t>(x >= two_times_modulus_small));

            std::uint64_t q_tmp = (static_cast<std::uint64_t>(y) *
                                   static_cast<std::uint64_t>(w_prime)) >>
                                  32;
            std::uint64_t q_new =
                w * static_cast<std::uint64_t>(y) -
                q_tmp * static_cast<std::uint64_t>(modulus_small);

            op[j] = curr_x + q_new;
            op[t + j] =
                curr_x +
                (static_cast<uint64_t>(two_times_modulus_small) - q_new);
          }
        } else {
          for (std::size_t j = 0; j < t; j += 4) {
            __m256i* p_x = reinterpret_cast<__m256i*>(&op[j]);
            __m256i* p_y = reinterpret_cast<__m256i*>(&op[j + t]);

            __m256i x = _mm256_loadu_si256(p_x);
            __m256i y = _mm256_loadu_si256(p_y);

            __m256i threshold_2q = _mm256_set1_epi64x(
                static_cast<int64_t>(two_times_modulus_small - 1));
            __m256i val_2q = _mm256_set1_epi64x(
                static_cast<int64_t>(two_times_modulus_small));

            __m256i gt_mask = _mm256_cmpgt_epi64(x, threshold_2q);
            __m256i to_subtract = _mm256_and_si256(gt_mask, val_2q);
            __m256i curr_x = _mm256_sub_epi64(x, to_subtract);

            __m256i w_prime_vec =
                _mm256_set1_epi64x(static_cast<int64_t>(w_prime));
            __m256i product = _mm256_mul_epu32(y, w_prime_vec);
            __m256i q_val = _mm256_srli_epi64(product, 32);

            __m256i w_vec = _mm256_set1_epi64x(static_cast<int64_t>(w));
            __m256i w_times_y = _mm256_mul_epu32(y, w_vec);

            __m256i modulus_small_vec =
                _mm256_set1_epi64x(static_cast<int64_t>(modulus_small));
            __m256i q_scaled = _mm256_mul_epu32(q_val, modulus_small_vec);
            __m256i q_final = _mm256_sub_epi64(w_times_y, q_scaled);

            __m256i new_x = _mm256_add_epi64(curr_x, q_final);
            __m256i q_final_inverted = _mm256_sub_epi64(val_2q, q_final);
            __m256i new_y = _mm256_add_epi64(curr_x, q_final_inverted);

            _mm256_storeu_si256(p_x, new_x);
            _mm256_storeu_si256(p_y, new_y);
          }
        }
      }
    }

    for (std::size_t i = 0; i < n; i += 4) {
      __m256i* p_x = reinterpret_cast<__m256i*>(&operand[i]);
      __m256i x = _mm256_loadu_si256(p_x);

      // Check >= 2q
      __m256i threshold_2q =
          _mm256_set1_epi64x(static_cast<int64_t>(two_times_modulus_small - 1));
      __m256i val_2q =
          _mm256_set1_epi64x(static_cast<int64_t>(two_times_modulus_small));

      __m256i gt_mask = _mm256_cmpgt_epi64(x, threshold_2q);
      __m256i to_subtract = _mm256_and_si256(gt_mask, val_2q);
      x = _mm256_sub_epi64(x, to_subtract);

      // Check >= q
      __m256i threshold_q =
          _mm256_set1_epi64x(static_cast<int64_t>(modulus_small - 1));
      __m256i val_q = _mm256_set1_epi64x(static_cast<int64_t>(modulus_small));

      gt_mask = _mm256_cmpgt_epi64(x, threshold_q);
      to_subtract = _mm256_and_si256(gt_mask, val_q);
      x = _mm256_sub_epi64(x, to_subtract);

      _mm256_storeu_si256(p_x, x);
    }
  }
}
#endif

#ifndef __AVX2__
void NttInverse(const Params& params, absl::Span<uint64_t> operand_overall) {
  for (std::size_t coeff_mod = 0; coeff_mod < params.CrtCount(); ++coeff_mod) {
    std::size_t n = params.PolyLen();
    auto operand = operand_overall.subspan(coeff_mod * n, n);

    const auto& inverse_table = params.GetNttInverseTable(coeff_mod);
    const auto& inverse_table_prime = params.GetNttInversePrimeTable(coeff_mod);
    std::uint64_t modulus = params.Moduli(coeff_mod);
    std::uint64_t two_times_modulus = 2 * modulus;

    for (std::size_t mm = params.PolyLenLog2(); mm-- > 0;) {
      std::size_t h = 1 << mm;
      std::size_t t = n >> (mm + 1);

      for (std::size_t i = 0; i < h; ++i) {
        uint64_t w = inverse_table[h + i];
        uint64_t w_prime = inverse_table_prime[h + i];

        auto op = operand.subspan(i * 2 * t, 2 * t);

        for (size_t j = 0; j < t; ++j) {
          uint64_t x = op[j];
          uint64_t y = op[t + j];

          uint64_t t_tmp = two_times_modulus - y + x;
          uint64_t curr_x =
              x + y -
              (two_times_modulus * static_cast<uint64_t>((x << 1) >= t_tmp));
          uint64_t h_tmp = (t_tmp * w_prime) >> 32;

          uint64_t res_x = (curr_x + (modulus * (t_tmp & 1))) >> 1;
          uint64_t res_y = w * t_tmp - h_tmp * modulus;

          op[j] = res_x;
          op[t + j] = res_y;
        }
      }
    }

    for (size_t i = 0; i < n; ++i) {
      operand[i] -= static_cast<uint64_t>(operand[i] >= two_times_modulus) *
                    two_times_modulus;
      operand[i] -= static_cast<uint64_t>(operand[i] >= modulus) * modulus;
    }
  }
}

#else

void NttInverse(const Params& params, absl::Span<uint64_t> operand_overall) {
  SPDLOG_DEBUG("using AVX2 NttInverse");

  for (size_t coeff_mod = 0; coeff_mod < params.CrtCount(); ++coeff_mod) {
    size_t n = params.PolyLen();
    auto operand = operand_overall.subspan(coeff_mod * n, n);

    const auto& inverse_table = params.GetNttInverseTable(coeff_mod);
    const auto& inverse_table_prime = params.GetNttInversePrimeTable(coeff_mod);
    uint64_t modulus = params.Moduli(coeff_mod);
    uint64_t two_times_modulus = 2 * modulus;

    for (size_t mm = params.PolyLenLog2(); mm-- > 0;) {
      size_t h = 1 << mm;
      size_t t = n >> (mm + 1);

      for (size_t i = 0; i < h; ++i) {
        uint64_t w = inverse_table[h + i];
        uint64_t w_prime = inverse_table_prime[h + i];

        auto op = operand.subspan(i * 2 * t, 2 * t);  // 获取当前的操作段
        if (op.size() < 2 * t) {
          throw std::runtime_error("Operation span is too small.");
        }

        if (t < 4) {
          for (size_t j = 0; j < t; ++j) {
            uint64_t x = op[j];
            uint64_t y = op[t + j];

            uint64_t t_tmp = two_times_modulus - y + x;
            uint64_t curr_x =
                x + y -
                (two_times_modulus * static_cast<uint64_t>((x << 1) >= t_tmp));
            uint64_t h_tmp = (t_tmp * w_prime) >> 32;

            uint64_t res_x = (curr_x + (modulus * (t_tmp & 1))) >> 1;
            uint64_t res_y = w * t_tmp - h_tmp * modulus;

            op[j] = res_x;
            op[t + j] = res_y;
          }
        } else {
          for (size_t j = 0; j < t; j += 4) {
            __m256i x =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&op[j]));
            __m256i y = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(&op[j + t]));

            __m256i modulus_vec =
                _mm256_set1_epi64x(static_cast<int64_t>(modulus));
            __m256i two_times_modulus_vec =
                _mm256_set1_epi64x(static_cast<int64_t>(two_times_modulus));
            __m256i t_tmp = _mm256_sub_epi64(two_times_modulus_vec, y);
            t_tmp = _mm256_add_epi64(t_tmp, x);

            // __m256i gt_mask =
            //     _mm256_cmpgt_epi64(_mm256_slli_epi64(x, 1), t_tmp);
            __m256i tmp1 = _mm256_cmpgt_epi64(_mm256_slli_epi64(x, 1), t_tmp);
            // __m256i to_subtract =
            //     _mm256_and_si256(tmp1, two_times_modulus_vec);

            tmp1 = _mm256_and_si256(tmp1, two_times_modulus_vec);

            __m256i curr_x = _mm256_add_epi64(x, y);
            curr_x = _mm256_sub_epi64(curr_x, tmp1);

            // __m256i w_prime_vec =
            //     _mm256_set1_epi64x(static_cast<int64_t>(w_prime));
            tmp1 = _mm256_set1_epi64x(static_cast<int64_t>(w_prime));
            __m256i h_tmp = _mm256_mul_epu32(t_tmp, tmp1);
            h_tmp = _mm256_srli_epi64(h_tmp, 32);

            // __m256i and_mask = _mm256_set1_epi64x(1);
            tmp1 = _mm256_set1_epi64x(1);
            __m256i eq_mask =
                _mm256_cmpeq_epi64(_mm256_and_si256(t_tmp, tmp1), tmp1);
            // __m256i to_add = _mm256_and_si256(eq_mask, modulus_vec);
            tmp1 = _mm256_and_si256(eq_mask, modulus_vec);

            // __m256i new_x =
            //     _mm256_srli_epi64(_mm256_add_epi64(curr_x, tmp1), 1);
            tmp1 = _mm256_srli_epi64(_mm256_add_epi64(curr_x, tmp1), 1);

            __m256i w_vec = _mm256_set1_epi64x(static_cast<int64_t>(w));
            __m256i w_times_t_tmp = _mm256_mul_epu32(t_tmp, w_vec);
            __m256i h_tmp_times_modulus = _mm256_mul_epu32(h_tmp, modulus_vec);
            __m256i new_y =
                _mm256_sub_epi64(w_times_t_tmp, h_tmp_times_modulus);

            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&op[j]), tmp1);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&op[j + t]), new_y);
          }
        }
      }
    }

    for (size_t i = 0; i < n; ++i) {
      operand[i] -= static_cast<uint64_t>(operand[i] >= two_times_modulus) *
                    two_times_modulus;
      operand[i] -= static_cast<uint64_t>(operand[i] >= modulus) * modulus;
    }
  }
}

#endif

}  // namespace psi::spiral::arith
