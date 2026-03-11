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

#include "psi/algorithm/ypir/ypir_util.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "yacl/base/exception.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif


namespace psi::ypir::byhe {
namespace {

int64_t ExGcd(int64_t a, int64_t b, int64_t& x, int64_t& y) {
  if (b == 0) {
    x = 1;
    y = 0;
    return a;
  }
  int64_t x1 = 0;
  int64_t y1 = 0;
  int64_t d = ExGcd(b, a % b, x1, y1);
  x = y1;
  y = x1 - (a / b) * y1;
  return d;
}

unsigned char ReverseByte(unsigned char x) {
  static const unsigned char table[] = {
      0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50,
      0xd0, 0x30, 0xb0, 0x70, 0xf0, 0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8,
      0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 0x04,
      0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4,
      0x34, 0xb4, 0x74, 0xf4, 0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c,
      0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, 0x02, 0x82,
      0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32,
      0xb2, 0x72, 0xf2, 0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
      0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, 0x06, 0x86, 0x46,
      0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6,
      0x76, 0xf6, 0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e,
      0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, 0x01, 0x81, 0x41, 0xc1,
      0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71,
      0xf1, 0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99,
      0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9, 0x05, 0x85, 0x45, 0xc5, 0x25,
      0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
      0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d,
      0xdd, 0x3d, 0xbd, 0x7d, 0xfd, 0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3,
      0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3, 0x0b,
      0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb,
      0x3b, 0xbb, 0x7b, 0xfb, 0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67,
      0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7, 0x0f, 0x8f,
      0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f,
      0xbf, 0x7f, 0xff,
  };
  return table[x];
}

uint32_t ReverseBits(uint32_t num, uint32_t msb) {
  static const int shift_trick[] = {0, 7, 6, 5, 4, 3, 2, 1};
  uint32_t msbb = (msb >> 3) + ((msb & 0x7) ? 1 : 0);
  switch (msbb) {
    case 1:
      return (ReverseByte(num & 0xff) >> shift_trick[msb & 0x7]);
    case 2:
      return (ReverseByte(num & 0xff) << 8 |
              ReverseByte((num >> 8) & 0xff)) >>
             shift_trick[msb & 0x7];
    case 3:
      return (ReverseByte(num & 0xff) << 16 |
              ReverseByte((num >> 8) & 0xff) << 8 |
              ReverseByte((num >> 16) & 0xff)) >>
             shift_trick[msb & 0x7];
    case 4:
      return (ReverseByte(num & 0xff) << 24 |
              ReverseByte((num >> 8) & 0xff) << 16 |
              ReverseByte((num >> 16) & 0xff) << 8 |
              ReverseByte((num >> 24) & 0xff)) >>
             shift_trick[msb & 0x7];
    default:
      YACL_ENFORCE(false, "ReverseBits msb out of range");
  }
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx512f,avx512bw,avx512vl")))
void MatVecU8U32Mod2p32Avx512(const uint8_t* A, const uint32_t* x,
                              uint32_t* y, size_t rows, size_t cols) {
  constexpr size_t kLanes = 16;

  for (size_t r = 0; r < rows; ++r) {
    const uint8_t* row = A + r * cols;
    __m512i acc0 = _mm512_setzero_si512();
    __m512i acc1 = _mm512_setzero_si512();

    size_t c = 0;
    for (; c + 2 * kLanes <= cols; c += 2 * kLanes) {
      const __m128i bytes0 =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + c));
      const __m128i bytes1 =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + c + kLanes));
      const __m512i a0 = _mm512_cvtepu8_epi32(bytes0);
      const __m512i a1 = _mm512_cvtepu8_epi32(bytes1);
      const __m512i x0 =
          _mm512_loadu_si512(reinterpret_cast<const __m512i*>(x + c));
      const __m512i x1 =
          _mm512_loadu_si512(reinterpret_cast<const __m512i*>(x + c + kLanes));
      acc0 = _mm512_add_epi32(acc0, _mm512_mullo_epi32(a0, x0));
      acc1 = _mm512_add_epi32(acc1, _mm512_mullo_epi32(a1, x1));
    }

    __m512i acc = _mm512_add_epi32(acc0, acc1);
    for (; c + kLanes <= cols; c += kLanes) {
      const __m128i bytes =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + c));
      const __m512i a = _mm512_cvtepu8_epi32(bytes);
      const __m512i xv =
          _mm512_loadu_si512(reinterpret_cast<const __m512i*>(x + c));
      acc = _mm512_add_epi32(acc, _mm512_mullo_epi32(a, xv));
    }

    alignas(64) uint32_t lanes[kLanes];
    _mm512_storeu_si512(reinterpret_cast<void*>(lanes), acc);
    uint32_t sum = 0;
    for (size_t i = 0; i < kLanes; ++i) {
      sum += lanes[i];
    }
    for (; c < cols; ++c) {
      sum += static_cast<uint32_t>(row[c]) * x[c];
    }
    y[r] = sum;
  }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
void MatrixVectorMultiplicationU16Avx512(
    std::vector<uint64_t>& result,
    const std::vector<std::vector<uint16_t>>& matrix,
    const std::vector<uint64_t>& vec, uint64_t mod) {
  const uint64_t mat_row = matrix.size();
  const uint64_t mat_col = mat_row == 0 ? 0 : matrix[0].size();
  result.assign(mat_row, 0);
  if (mat_row == 0 || mat_col == 0) {
    return;
  }

  std::vector<uint64_t> row64(mat_col, 0);
  std::vector<uint64_t> tmp(mat_col, 0);
  constexpr uint64_t kBlock = 128;

  for (uint64_t i = 0; i < mat_row; ++i) {
    for (uint64_t j = 0; j < mat_col; ++j) {
      row64[j] = static_cast<uint64_t>(matrix[i][j]);
    }

    EltwiseMultMod(tmp.data(), row64.data(), vec.data(), mat_col, mod);

    uint64_t sum_mod = 0;
    for (uint64_t base = 0; base < mat_col; base += kBlock) {
      const uint64_t end = std::min<uint64_t>(base + kBlock, mat_col);
      __m512i acc0 = _mm512_setzero_si512();
      __m512i acc1 = _mm512_setzero_si512();
      uint64_t j = base;
      for (; j + 16 <= end; j += 16) {
        const __m512i v0 = _mm512_loadu_si512(
            reinterpret_cast<const __m512i*>(tmp.data() + j));
        const __m512i v1 = _mm512_loadu_si512(
            reinterpret_cast<const __m512i*>(tmp.data() + j + 8));
        acc0 = _mm512_add_epi64(acc0, v0);
        acc1 = _mm512_add_epi64(acc1, v1);
      }

      const __m512i acc = _mm512_add_epi64(acc0, acc1);
      alignas(64) uint64_t buf[8];
      _mm512_store_si512(reinterpret_cast<__m512i*>(buf), acc);

      unsigned __int128 acc128 = 0;
      for (uint64_t k = 0; k < 8; ++k) {
        acc128 += buf[k];
      }
      for (; j < end; ++j) {
        acc128 += tmp[j];
      }
      sum_mod = (sum_mod + static_cast<uint64_t>(acc128 % mod)) % mod;
    }
    result[i] = sum_mod;
  }
}
#endif
#endif

void MatVecU8U32Mod2p32Scalar(const uint8_t* A, const uint32_t* x, uint32_t* y,
                              size_t rows, size_t cols) {
  for (size_t r = 0; r < rows; ++r) {
    uint64_t acc = 0;
    const uint8_t* row = A + r * cols;
    for (size_t c = 0; c < cols; ++c) {
      acc += static_cast<uint64_t>(row[c]) * x[c];
    }
    y[r] = static_cast<uint32_t>(acc);
  }
}

}  // namespace

uint64_t GetLog2(uint64_t num) {
  uint64_t result = 0;
  while (num > 1) {
    num >>= 1;
    ++result;
  }
  return result;
}

bool IsPowerOfTwo(uint64_t num) { return num != 0 && (num & (num - 1)) == 0; }

uint64_t SampleGauss(double st_dev, uint64_t modulus, std::mt19937_64& rng) {
  std::normal_distribution<double> gaussian_sampler(0.0, st_dev);
  int64_t tmp = static_cast<int64_t>(std::llround(gaussian_sampler(rng)));
  if (tmp < 0) {
    return static_cast<uint64_t>(modulus + tmp);
  }
  return static_cast<uint64_t>(tmp);
}

void SampleGauss(std::vector<uint64_t>& err, double st_dev, uint64_t modulus,
                 std::mt19937_64& rng) {
  std::normal_distribution<double> gaussian_sampler(0.0, st_dev);
  for (auto& v : err) {
    int64_t tmp = static_cast<int64_t>(std::llround(gaussian_sampler(rng)));
    if (tmp < 0) {
      v = static_cast<uint64_t>(modulus + tmp);
    } else {
      v = static_cast<uint64_t>(tmp);
    }
  }
}

int64_t ModInverse(int64_t a, int64_t modulus) {
  int64_t x = 0;
  int64_t y = 0;
  int64_t g = ExGcd(a, modulus, x, y);
  if (g != 1) {
    return -1;
  }
  int64_t res = (x % modulus + modulus) % modulus;
  return res;
}

void ApplyAutoCoefForm(std::vector<uint64_t>& result,
                       const std::vector<uint64_t>& input, int32_t index,
                       uint64_t modulus) {
  const uint64_t length = input.size();
  result.assign(length, 0);
  for (size_t i = 0; i < length; ++i) {
    uint64_t destination = (static_cast<uint64_t>(i) * index) % (2 * length);
    if (destination >= length) {
      result[destination - length] = (modulus - input[i]) % modulus;
    } else {
      result[destination] = input[i];
    }
  }
}

void MatrixTranspose(const std::vector<std::vector<uint64_t>>& matrix,
                     std::vector<std::vector<uint64_t>>& trans_matrix) {
  const uint64_t rows = matrix.size();
  const uint64_t cols = rows == 0 ? 0 : matrix[0].size();
  trans_matrix.assign(cols, std::vector<uint64_t>(rows, 0));
  constexpr uint64_t kBlock = 64;
  for (uint64_t ii = 0; ii < cols; ii += kBlock) {
    for (uint64_t jj = 0; jj < rows; jj += kBlock) {
      uint64_t i_end = std::min<uint64_t>(ii + kBlock, cols);
      uint64_t j_end = std::min<uint64_t>(jj + kBlock, rows);
      for (uint64_t i = ii; i < i_end; ++i) {
        for (uint64_t j = jj; j < j_end; ++j) {
          trans_matrix[i][j] = matrix[j][i];
        }
      }
    }
  }
}

void BitDecomp(uint64_t num, std::vector<uint64_t>& vec, uint64_t b, uint64_t z,
               uint64_t t) {
  num >>= b;
  vec.assign(t, 0);
  const uint64_t mask = (1ULL << z) - 1;
  for (uint64_t i = 0; i < t; ++i) {
    vec[i] = num & mask;
    num >>= z;
  }
}

void PrecomputeAutomap(uint32_t length, uint32_t idx,
                       std::vector<uint32_t>& automap) {
  const uint32_t m = length << 1;
  const uint32_t logm = static_cast<uint32_t>(std::llround(std::log2(m)));
  const uint32_t logn = static_cast<uint32_t>(std::llround(std::log2(length)));
  automap.assign(length, 0);
  for (uint32_t j = 0; j < length; ++j) {
    uint32_t j_tmp = ((j << 1) + 1);
    uint32_t index = ((j_tmp * idx) - (((j_tmp * idx) >> logm) << logm)) >> 1;
    uint32_t j_rev = ReverseBits(j, logn);
    uint32_t idx_rev = ReverseBits(index, logn);
    automap[j_rev] = idx_rev;
  }
}

void PseudorandomMatrixGenerate(std::vector<std::vector<uint64_t>>& matrix,
                                uint64_t modulus, AESCTR_PRNG& prng) {
  const uint64_t rows = matrix.size();
  const uint64_t cols = rows == 0 ? 0 : matrix[0].size();
  for (uint64_t i = 0; i < rows; ++i) {
    uint8_t* ptr = reinterpret_cast<uint8_t*>(matrix[i].data());
    prng.fill_bytes(ptr, sizeof(uint64_t) * cols);
  }
  for (uint64_t i = 0; i < rows; ++i)
    for (uint64_t j = 0; j < cols; ++j)
      matrix[i][j] %= modulus;
}

void SetPseudorandomMatrixSimplepir(std::vector<std::vector<uint64_t>>& matrix,
                                    std::vector<uint64_t>& matrix_flat,
                                    uint64_t rows, uint64_t cols,
                                    uint64_t modulus, AESCTR_PRNG& prng) {
  matrix.assign(rows, std::vector<uint64_t>(cols, 0));
  prng.refresh(kFirstDimensionSeed);
  PseudorandomMatrixGenerate(matrix, modulus, prng);
  matrix_flat.resize(rows * cols);
  for (uint64_t i = 0; i < rows; ++i) {
    std::memcpy(&matrix_flat[i * cols], matrix[i].data(),
                cols * sizeof(uint64_t));
  }
}

void SetPseudorandomMatrixDoublepir(std::vector<std::vector<uint64_t>>& matrix,
                                    std::vector<uint64_t>& matrix_flat,
                                    uint64_t rows, uint64_t cols,
                                    uint64_t modulus, AESCTR_PRNG& prng) {
  matrix.assign(rows, std::vector<uint64_t>(cols, 0));
  prng.refresh(kSecondDimensionSeed);
  PseudorandomMatrixGenerate(matrix, modulus, prng);
  matrix_flat.resize(rows * cols);
  for (uint64_t i = 0; i < rows; ++i) {
    std::memcpy(&matrix_flat[i * cols], matrix[i].data(),
                cols * sizeof(uint64_t));
  }
}

void SetPseudorandomHypercubeYpir(
    std::vector<std::vector<std::vector<uint64_t>>>& cube, uint64_t layers,
    uint64_t rows, uint64_t cols, uint64_t modulus, AESCTR_PRNG& prng) {
  cube.assign(layers,
              std::vector<std::vector<uint64_t>>(rows,
                                                 std::vector<uint64_t>(cols)));
  prng.refresh(kThirdDimensionSeed);
  for (uint64_t i = 0; i < layers; ++i) {
    PseudorandomMatrixGenerate(cube[i], modulus, prng);
  }
}

void SetAutomap(std::vector<std::vector<uint32_t>>& automap, uint64_t degree,
                const std::vector<uint64_t>& idx) {
  automap.assign(idx.size(), std::vector<uint32_t>(degree, 0));
  for (size_t i = 0; i < idx.size(); ++i) {
    PrecomputeAutomap(static_cast<uint32_t>(degree),
                      static_cast<uint32_t>(idx[i]), automap[i]);
  }
}

void SetPrecomputedPt(std::vector<std::vector<uint64_t>>& precomputed_pt,
                      uint64_t poly_degree, uint64_t max_lh,
                      NttForwardFn ntt_forward) {
  precomputed_pt.assign(max_lh + 1, std::vector<uint64_t>(poly_degree, 0));
  for (uint64_t lh = 2; lh <= max_lh; ++lh) {
    precomputed_pt[lh][poly_degree / (1ULL << lh)] = 1;
    if (ntt_forward != nullptr) {
      ntt_forward(precomputed_pt[lh].data(), precomputed_pt[lh].size());
    }
  }
}

uint64_t GetAutokeyIdx(uint64_t idx, uint64_t expo) {
  return expo - GetLog2(idx - 1);
}

void VectorColDecompose(const std::vector<uint32_t>& vec,
                        std::vector<std::vector<uint16_t>>& decomp_matrix,
                        uint64_t b, uint64_t z, uint64_t t) {
  const uint64_t row = vec.size();
  const uint64_t mask = (1ULL << z) - 1;
  decomp_matrix.assign(t, std::vector<uint16_t>(row, 0));
  for (uint64_t i = 0; i < t; ++i) {
    for (uint64_t j = 0; j < row; ++j) {
      decomp_matrix[i][j] =
          static_cast<uint16_t>((vec[j] >> (b + i * z)) & mask);
    }
  }
}

void MatrixRowDecompose(const std::vector<std::vector<uint64_t>>& matrix,
                        std::vector<std::vector<uint64_t>>& decomp_matrix,
                        uint64_t b, uint64_t z, uint64_t t) {
  const uint64_t row = matrix.size();
  const uint64_t col = row == 0 ? 0 : matrix[0].size();
  decomp_matrix.assign(row, std::vector<uint64_t>(col * t, 0));
  for (uint64_t i = 0; i < row; ++i) {
    std::vector<uint64_t> tmp(t, 0);
    uint64_t ptr = 0;
    for (uint64_t j = 0; j < col; ++j) {
      BitDecomp(matrix[i][j], tmp, b, z, t);
      for (uint64_t k = 0; k < t; ++k, ++ptr) {
        decomp_matrix[i][ptr] = tmp[k];
      }
    }
  }
}

void MatrixVectorFirstDimension(std::vector<uint64_t>& result,
                                const std::vector<std::vector<uint64_t>>& matrix,
                                const std::vector<uint64_t>& vec,
                                uint64_t mod) {
  const uint64_t rows = matrix.size();
  const uint64_t cols = rows == 0 ? 0 : matrix[0].size();
  result.assign(cols, 0);
  for (uint64_t r = 0; r < rows; ++r) {
    EltwiseFMAMod(result.data(), matrix[r].data(), vec[r], result.data(), cols,
                  mod);
  }
}

void MatrixMultiplicationFlat(
    std::vector<std::vector<uint64_t>>& db_mul_matrix,
    const std::vector<std::vector<uint64_t>>& db, const uint64_t* matrix_flat,
    uint64_t matrix_col, uint64_t mod) {
  const uint64_t db_row = db.size();
  const uint64_t db_col = db_row == 0 ? 0 : db[0].size();
  db_mul_matrix.assign(db_row, std::vector<uint64_t>(matrix_col, 0));
  for (uint64_t i = 0; i < db_row; ++i) {
    uint64_t* res = db_mul_matrix[i].data();
    for (uint64_t k = 0; k < db_col; ++k)
      EltwiseFMAMod(res, matrix_flat + k * matrix_col, db[i][k], res,
                    matrix_col, mod);
  }
}


void MatrixMultiplicationFlatU16(
    std::vector<std::vector<uint64_t>>& db_mul_matrix,
    const std::vector<std::vector<uint16_t>>& db, const uint64_t* matrix_flat,
    uint64_t matrix_col, uint64_t mod) {
  const uint64_t db_row = db.size();
  const uint64_t db_col = db_row == 0 ? 0 : db[0].size();
  db_mul_matrix.assign(db_row, std::vector<uint64_t>(matrix_col, 0));
  for (uint64_t i = 0; i < db_row; ++i) {
    uint64_t* res = db_mul_matrix[i].data();
    const uint16_t* row = db[i].data();
    for (uint64_t k = 0; k < db_col; ++k) {
      const uint64_t s = row[k];
      if (s == 0) continue;
      EltwiseFMAMod(res, matrix_flat + k * matrix_col, s, res, matrix_col, mod);
    }
  }
}

void MatrixVectorMultiplicationU16(
    std::vector<uint64_t>& result,
    const std::vector<std::vector<uint16_t>>& matrix,
    const std::vector<uint64_t>& vec, uint64_t mod) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
  MatrixVectorMultiplicationU16Avx512(result, matrix, vec, mod);
  return;
#endif
#endif
  const uint64_t mat_row = matrix.size();
  const uint64_t mat_col = mat_row == 0 ? 0 : matrix[0].size();
  result.assign(mat_row, 0);
  for (uint64_t i = 0; i < mat_row; ++i) {
    unsigned __int128 sum = 0;
    for (uint64_t j = 0; j < mat_col; ++j) {
      sum += static_cast<unsigned __int128>(matrix[i][j]) * vec[j];
      if ((j & 127U) == 127U) {
        sum %= mod;
      }
    }
    result[i] = static_cast<uint64_t>(sum % mod);
  }
}

void MatVecU8U32Mod2p32(const uint8_t* A, const uint32_t* x, uint32_t* y,
                        size_t rows, size_t cols) {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
  MatVecU8U32Mod2p32Avx512(A, x, y, rows, cols);
  return;
#endif
#endif
  MatVecU8U32Mod2p32Scalar(A, x, y, rows, cols);
}

}  // namespace psi::ypir::byhe
