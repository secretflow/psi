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

#include "psi/algorithm/spiral/poly_matrix_utils.h"

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include "absl/types/span.h"
#include "seal/modulus.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/arith/ntt.h"
#include "psi/algorithm/spiral/discrete_gaussian.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

//----some utils method for PolyMatrix-----

void MultiplyPoly(const Params& params, absl::Span<uint64_t> res,
                  absl::Span<const uint64_t> a, absl::Span<const uint64_t> b) {
  // todo: try parallel
  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); ++i) {
      size_t idx = c * params.PolyLen() + i;
      res[idx] = arith::MultiplyModular(params, a[idx], b[idx], c);
    }
  }
}

#ifndef __AVX2__

void MultiplyAddPoly(const Params& params, absl::Span<uint64_t> res,
                     absl::Span<const uint64_t> a,
                     absl::Span<const uint64_t> b) {
  WEAK_ENFORCE(res.size() == a.size());
  WEAK_ENFORCE(res.size() == b.size());
  WEAK_ENFORCE(res.size() == params.CrtCount() * params.PolyLen());
  //
  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); ++i) {
      size_t idx = c * params.PolyLen() + i;
      res[idx] = arith::MultiplyAddModular(params, a[idx], b[idx], res[idx], c);
    }
  }
}

#else

void MultiplyAddPolyAvx2(const Params& params, absl::Span<uint64_t> res,
                         absl::Span<const uint64_t> a,
                         absl::Span<const uint64_t> b) {
  WEAK_ENFORCE(res.size() == a.size());
  WEAK_ENFORCE(res.size() == b.size());
  WEAK_ENFORCE(res.size() == params.CrtCount() * params.PolyLen());

  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); i += 4) {
      const __m256i* p_x =
          reinterpret_cast<const __m256i*>(&a[c * params.PolyLen() + i]);
      const __m256i* p_y =
          reinterpret_cast<const __m256i*>(&b[c * params.PolyLen() + i]);
      __m256i* p_z = reinterpret_cast<__m256i*>(&res[c * params.PolyLen() + i]);

      // Load the data into AVX2 registers
      __m256i x = _mm256_loadu_si256(p_x);
      __m256i y = _mm256_loadu_si256(p_y);
      __m256i z = _mm256_loadu_si256(p_z);

      // Perform the multiplication and addition
      __m256i product = _mm256_mul_epu32(x, y);
      __m256i out = _mm256_add_epi64(z, product);
      _mm256_storeu_si256(p_z, out);
    }
  }
}

void MultiplyAddPoly(const Params& params, absl::Span<uint64_t> res,
                     absl::Span<const uint64_t> a,
                     absl::Span<const uint64_t> b) {
  MultiplyAddPolyAvx2(params, res, a, b);
  ReducePoly(params, res);
}

#endif

void AddPoly(const Params& params, absl::Span<uint64_t> res,
             absl::Span<const uint64_t> a, absl::Span<const uint64_t> b) {
  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); ++i) {
      size_t idx = c * params.PolyLen() + i;
      res[idx] = arith::AddModular(params, a[idx], b[idx], c);
    }
  }
}

void AddPolyInto(const Params& params, absl::Span<uint64_t> res,
                 absl::Span<const uint64_t> a) {
  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); ++i) {
      size_t idx = c * params.PolyLen() + i;
      res[idx] = arith::AddModular(params, a[idx], res[idx], c);
    }
  }
}

void InvertPoly(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> a) {
  for (size_t i = 0; i < params.PolyLen(); ++i) {
    res[i] = params.Modulus() - a[i];
  }
}

void AutomotphPoly(const Params& params, absl::Span<uint64_t> res,
                   absl::Span<const uint64_t> a, size_t t) {
  auto poly_len = params.PolyLen();
  for (size_t i = 0; i < poly_len; ++i) {
    uint64_t num = (i * t) / poly_len;
    uint64_t rem = (i * t) % poly_len;
    if (num % 2 == 0) {
      res[rem] = a[i];
    } else {
      res[rem] = params.Modulus() - a[i];
    }
  }
}

void ReduceCopy(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> in) {
  for (size_t i = 0; i < params.CrtCount(); ++i) {
    for (size_t j = 0; j < params.PolyLen(); ++j) {
      res[i * params.PolyLen() + j] = arith::BarrettCoeffU64(params, in[j], i);
    }
  }
}

void ReducePoly(const Params& params, absl::Span<uint64_t> res) {
  WEAK_ENFORCE(res.size() == params.CrtCount() * params.PolyLen());
  for (size_t c = 0; c < params.CrtCount(); ++c) {
    for (size_t i = 0; i < params.PolyLen(); ++i) {
      size_t idx = c * params.PolyLen() + i;
      res[idx] = arith::BarrettCoeffU64(params, res[idx], c);
    }
  }
}

PolyMatrixRaw Noise(const Params& params, size_t rows, size_t cols,
                    const DiscreteGaussian& dg,
                    yacl::crypto::Prg<uint64_t>& prg) {
  PolyMatrixRaw out = PolyMatrixRaw::Zero(params.PolyLen(), rows, cols);
  dg.SampleMatrix(params, out, prg);
  return out;
}

void GenTernaryMatrix(const Params& params, PolyMatrixRaw& mat, size_t hamming,
                      yacl::crypto::Prg<uint64_t>& prg) {
  auto modulus = params.Modulus();

  uint128_t mt_seed;
  prg.Fill(
      absl::MakeSpan(reinterpret_cast<uint8_t*>(&mt_seed), sizeof(mt_seed)));
  std::mt19937 rng(mt_seed);
  // todo: change the style
  for (size_t i = 0; i < mat.Rows(); ++i) {
    for (size_t j = 0; j < mat.Cols(); ++j) {
      auto poly = mat.Poly(i, j);
      for (size_t k = 0; k < hamming; ++k) {
        poly[k] = 1;
        poly[k + hamming] = modulus - 1;
      }
      // shuffle
      std::shuffle(poly.begin(), poly.end(), rng);
    }
  }
}

PolyMatrixNtt ShiftRowsByOne(const PolyMatrixNtt& in) {
  if (in.Rows() == 1) {
    return PolyMatrixNtt(in);
  }
  auto sub_rows = in.SubMatrix(0, 0, in.Rows() - 1, in.Cols());
  auto last_row = in.SubMatrix(in.Rows() - 1, 0, 1, in.Cols());
  auto out = StackNtt(last_row, sub_rows);
  return out;
}

PolyMatrixNtt StackNtt(const PolyMatrixNtt& a, const PolyMatrixNtt& b) {
  WEAK_ENFORCE(a.Cols() == b.Cols());
  auto c = PolyMatrixNtt::Zero(a.CrtCount(), a.PolyLen(), a.Rows() + b.Rows(),
                               a.Cols());
  c.CopyInto(a, 0, 0);
  c.CopyInto(b, a.Rows(), 0);
  return c;
}

PolyMatrixRaw Stack(const PolyMatrixRaw& a, const PolyMatrixRaw& b) {
  WEAK_ENFORCE(a.Cols() == b.Cols());
  auto c = PolyMatrixRaw::Zero(a.PolyLen(), a.Rows() + b.Rows(), a.Cols());
  c.CopyInto(a, 0, 0);
  c.CopyInto(b, a.Rows(), 0);
  return c;
}

void ScalarMultiply(const Params& params, PolyMatrixNtt& res,
                    const PolyMatrixNtt& a, const PolyMatrixNtt& b) {
  WEAK_ENFORCE(a.Rows() == 1 && a.Cols() == 1);
  auto poly_a = a.Poly(0, 0);
  for (size_t i = 0; i < b.Rows(); ++i) {
    for (size_t j = 0; j < b.Cols(); ++j) {
      auto poly_b = b.Poly(i, j);
      auto poly_res = res.Poly(i, j);
      // mul
      MultiplyPoly(params, poly_res, poly_a, poly_b);
    }
  }
}

PolyMatrixNtt ScalarMultiply(const Params& params, const PolyMatrixNtt& a,
                             const PolyMatrixNtt& b) {
  PolyMatrixNtt res =
      PolyMatrixNtt::Zero(b.CrtCount(), b.PolyLen(), b.Rows(), b.Cols());
  ScalarMultiply(params, res, a, b);
  return res;
}

void Automorphism(const Params& params, PolyMatrixRaw& res,
                  const PolyMatrixRaw& a, size_t t) {
  WEAK_ENFORCE(res.Rows() == a.Rows());
  WEAK_ENFORCE(res.Cols() == a.Cols());

  // handle each poly
  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto poly_a = a.Poly(i, j);
      auto poly_res = res.Poly(i, j);
      AutomotphPoly(params, poly_res, poly_a, t);
    }
  }
}

PolyMatrixRaw Automorphism(const Params& params, const PolyMatrixRaw& a,
                           size_t t) {
  PolyMatrixRaw res = PolyMatrixRaw::Zero(a.PolyLen(), a.Rows(), a.Cols());
  Automorphism(params, res, a, t);
  return res;
}

void Add(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
         const PolyMatrixNtt& b) {
  WEAK_ENFORCE(res.Rows() == a.Rows());
  WEAK_ENFORCE(res.Cols() == a.Cols());
  WEAK_ENFORCE(a.Rows() == b.Rows());
  WEAK_ENFORCE(a.Cols() == b.Cols());

  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      auto a_poly = a.Poly(i, j);
      auto b_poly = b.Poly(i, j);
      AddPoly(params, res_poly, a_poly, b_poly);
    }
  }
}

PolyMatrixNtt Add(const Params& params, const PolyMatrixNtt& a,
                  const PolyMatrixNtt& b) {
  WEAK_ENFORCE(a.Rows() == b.Rows());
  WEAK_ENFORCE(a.Cols() == b.Cols());

  PolyMatrixNtt res(a);

  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      auto a_poly = a.Poly(i, j);
      auto b_poly = b.Poly(i, j);
      AddPoly(params, res_poly, a_poly, b_poly);
    }
  }
  return res;
}

void AddInto(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a) {
  WEAK_ENFORCE(res.Rows() == a.Rows());
  WEAK_ENFORCE(res.Cols() == a.Cols());
  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      auto a_poly = a.Poly(i, j);
      AddPolyInto(params, res_poly, a_poly);
    }
  }
}

void AddIntoAt(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
               size_t t_row, size_t t_col) {
  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(t_row + i, t_col + j);
      auto a_poly = a.Poly(i, j);
      AddPolyInto(params, res_poly, a_poly);
    }
  }
}

void Invert(const Params& params, PolyMatrixRaw& res, const PolyMatrixRaw& a) {
  WEAK_ENFORCE(res.Rows() == a.Rows());
  WEAK_ENFORCE(res.Cols() == a.Cols());

  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      auto a_poly = a.Poly(i, j);
      InvertPoly(params, res_poly, a_poly);
    }
  }
}

PolyMatrixRaw Invert(const Params& params, const PolyMatrixRaw& a) {
  PolyMatrixRaw res(params.PolyLen(), a.Rows(), a.Cols());
  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      auto a_poly = a.Poly(i, j);
      InvertPoly(params, res_poly, a_poly);
    }
  }
  return res;
}

void FromNtt(const Params& params, PolyMatrixRaw& out,
             const PolyMatrixNtt& in) {
  WEAK_ENFORCE(out.Rows() == in.Rows());
  WEAK_ENFORCE(out.Cols() == in.Cols());

  for (size_t r = 0; r < out.Rows(); ++r) {
    for (size_t c = 0; c < out.Cols(); ++c) {
      // get cur ntt poly
      auto in_poly = in.Poly(r, c);
      // deep copy into another vector to avoid change the in matrix
      std::vector<uint64_t> temp(in_poly.begin(), in_poly.end());
      arith::NttInverse(params, absl::MakeSpan(temp));
      size_t raw_poly_idx = out.PolyStartIndex(r, c);
      // compose
      for (size_t i = 0; i < params.PolyLen(); ++i) {
        out.Data()[raw_poly_idx + i] = params.CrtCompose(temp, i);
      }
    }
  }
}

PolyMatrixRaw FromNtt(const Params& params, const PolyMatrixNtt& in) {
  PolyMatrixRaw res =
      PolyMatrixRaw::Zero(params.PolyLen(), in.Rows(), in.Cols());
  FromNtt(params, res, in);
  return res;
}

void ToNtt(const Params& params, PolyMatrixNtt& out, const PolyMatrixRaw& in) {
  for (size_t r = 0; r < out.Rows(); ++r) {
    for (size_t c = 0; c < out.Cols(); ++c) {
      auto in_poly = in.Poly(r, c);
      auto out_poly = out.Poly(r, c);
      ReduceCopy(params, out_poly, in_poly);
      arith::NttForward(params, out_poly);
    }
  }
}

PolyMatrixNtt ToNtt(const Params& params, const PolyMatrixRaw& in) {
  PolyMatrixNtt out = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(),
                                          in.Rows(), in.Cols());
  ToNtt(params, out, in);
  return out;
}

void ToNttNoReduce(const Params& params, PolyMatrixNtt& out,
                   const PolyMatrixRaw& in) {
  for (size_t r = 0; r < out.Rows(); ++r) {
    for (size_t c = 0; c < out.Cols(); ++c) {
      auto in_poly = in.Poly(r, c);
      auto out_poly = out.Poly(r, c);
      // copy in_poly into 2-RNS moduli
      std::memcpy(out_poly.data(), in_poly.data(),
                  in_poly.size() * sizeof(uint64_t));

      std::memcpy(out_poly.data() + in_poly.size(), in_poly.data(),
                  in_poly.size() * sizeof(uint64_t));
      // NTT
      arith::NttForward(params, out_poly);
    }
  }
}

void Multiply(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
              const PolyMatrixNtt& b) {
  WEAK_ENFORCE(res.Rows() == a.Rows());
  WEAK_ENFORCE(res.Cols() == b.Cols());
  WEAK_ENFORCE(a.Cols() == b.Rows());

  WEAK_ENFORCE(res.NumWords() == a.NumWords());
  WEAK_ENFORCE(a.NumWords() == b.NumWords());

  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < b.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      std::fill(res_poly.begin(), res_poly.end(), 0);
      for (size_t k = 0; k < a.Cols(); ++k) {
        auto a_poly = a.Poly(i, k);
        auto b_poly = b.Poly(k, j);
        MultiplyAddPoly(params, res_poly, a_poly, b_poly);
      }
    }
  }
}

PolyMatrixNtt Multiply(const Params& params, const PolyMatrixNtt& a,
                       const PolyMatrixNtt& b) {
  WEAK_ENFORCE(a.Cols() == b.Rows());
  WEAK_ENFORCE(a.NumWords() == b.NumWords());

  PolyMatrixNtt res =
      PolyMatrixNtt::Zero(a.CrtCount(), a.PolyLen(), a.Rows(), b.Cols());

  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < b.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      std::fill(res_poly.begin(), res_poly.end(), 0);
      for (size_t k = 0; k < a.Cols(); ++k) {
        auto a_poly = a.Poly(i, k);
        auto b_poly = b.Poly(k, j);
        MultiplyAddPoly(params, res_poly, a_poly, b_poly);
      }
    }
  }

  return res;
}

PolyMatrixRaw MatrixWithIdentity(const PolyMatrixRaw& p) {
  WEAK_ENFORCE(p.Cols() == 1U);

  auto r = PolyMatrixRaw::Zero(p.PolyLen(), p.Rows(), p.Rows() + 1);
  // copy p to r
  r.CopyInto(p, 0, 0);
  // concatenate a identity matrixa
  auto identity = PolyMatrixRaw::Identity(p.PolyLen(), p.Rows(), p.Rows());
  r.CopyInto(identity, 0, 1);
  return r;
}

}  // namespace psi::spiral
