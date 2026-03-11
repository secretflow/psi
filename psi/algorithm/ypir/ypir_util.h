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
#include <random>
#include <vector>

#include "psi/algorithm/ypir/aes_prng.h"
#include "psi/algorithm/ypir/hexl.h"

namespace psi::ypir::byhe {

class FheParams;

constexpr uint64_t kFirstDimensionSeed = 1;
constexpr uint64_t kSecondDimensionSeed = 1ULL << 16;
constexpr uint64_t kThirdDimensionSeed = 1ULL << 16;

uint64_t GetLog2(uint64_t num);
bool IsPowerOfTwo(uint64_t num);
uint64_t GetAutokeyIdx(uint64_t idx, uint64_t expo);

uint64_t SampleGauss(double st_dev, uint64_t modulus,
                     std::mt19937_64& rng);
void SampleGauss(std::vector<uint64_t>& err, double st_dev, uint64_t modulus,
                 std::mt19937_64& rng);

int64_t ModInverse(int64_t a, int64_t modulus);

void ApplyAutoCoefForm(std::vector<uint64_t>& result,
                       const std::vector<uint64_t>& input, int32_t index,
                       uint64_t modulus);

void MatrixTranspose(const std::vector<std::vector<uint64_t>>& matrix,
                     std::vector<std::vector<uint64_t>>& trans_matrix);

void PrecomputeAutomap(uint32_t length, uint32_t idx,
                       std::vector<uint32_t>& automap);

void PseudorandomMatrixGenerate(std::vector<std::vector<uint64_t>>& matrix,
                                uint64_t modulus, AESCTR_PRNG& prng);

// Helpers for byhe-style precomputation buffers.
void SetPseudorandomMatrixSimplepir(std::vector<std::vector<uint64_t>>& matrix,
                                    std::vector<uint64_t>& matrix_flat,
                                    uint64_t rows, uint64_t cols,
                                    uint64_t modulus, AESCTR_PRNG& prng);

void SetPseudorandomMatrixDoublepir(std::vector<std::vector<uint64_t>>& matrix,
                                    std::vector<uint64_t>& matrix_flat,
                                    uint64_t rows, uint64_t cols,
                                    uint64_t modulus, AESCTR_PRNG& prng);

void SetPseudorandomHypercubeYpir(
    std::vector<std::vector<std::vector<uint64_t>>>& cube, uint64_t layers,
    uint64_t rows, uint64_t cols, uint64_t modulus, AESCTR_PRNG& prng);

void SetAutomap(std::vector<std::vector<uint32_t>>& automap, uint64_t degree,
                const std::vector<uint64_t>& idx);

using NttForwardFn = void (*)(uint64_t* data, size_t len);

// If ntt_forward is nullptr, precomputed polynomials stay in coefficient form.
void SetPrecomputedPt(std::vector<std::vector<uint64_t>>& precomputed_pt,
                      uint64_t poly_degree, uint64_t max_lh,
                      NttForwardFn ntt_forward);

// byhe server-side helpers
void VectorColDecompose(const std::vector<uint32_t>& vec,
                        std::vector<std::vector<uint16_t>>& decomp_matrix,
                        uint64_t b, uint64_t z, uint64_t t);

void MatrixRowDecompose(const std::vector<std::vector<uint64_t>>& matrix,
                        std::vector<std::vector<uint64_t>>& decomp_matrix,
                        uint64_t b, uint64_t z, uint64_t t);

void MatrixVectorFirstDimension(std::vector<uint64_t>& result,
                                const std::vector<std::vector<uint64_t>>& matrix,
                                const std::vector<uint64_t>& vec,
                                uint64_t mod);

void MatrixMultiplicationFlat(
    std::vector<std::vector<uint64_t>>& db_mul_matrix,
    const std::vector<std::vector<uint64_t>>& db, const uint64_t* matrix_flat,
    uint64_t matrix_col, uint64_t mod);

void MatrixMultiplicationFlatU16(
    std::vector<std::vector<uint64_t>>& db_mul_matrix,
    const std::vector<std::vector<uint16_t>>& db, const uint64_t* matrix_flat,
    uint64_t matrix_col, uint64_t mod);

void MatrixVectorMultiplicationU16(
    std::vector<uint64_t>& result, const std::vector<std::vector<uint16_t>>& matrix,
    const std::vector<uint64_t>& vec, uint64_t mod);

void MatVecU8U32Mod2p32(const uint8_t* A, const uint32_t* x, uint32_t* y,
                        size_t rows, size_t cols);

std::vector<uint64_t> PackrlwePreprocess(
    std::vector<std::vector<uint64_t>>& a, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_a,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
    const FheParams& fparm);

std::vector<uint64_t> PackrlweOnline(
    std::vector<std::vector<uint64_t>>& b, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm);

std::vector<uint64_t> PackrlweOnlineConstantRows(
    const std::vector<uint64_t>& b, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm);

void Cdks21Lwe2RlweInplace(uint64_t* lwe_a, uint64_t degree, uint64_t cmod,
                           ByheHexlNtt& ntt);

}  // namespace psi::ypir::byhe
