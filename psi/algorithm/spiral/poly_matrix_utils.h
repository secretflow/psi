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

#pragma once

#include <memory>

#include "absl/types/span.h"
#include "seal/modulus.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/discrete_gaussian.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"

namespace psi::spiral {

//----some utils method for PolyMatrix-----

// Poly operators

// res = a * b
void MultiplyPoly(const Params& params, absl::Span<uint64_t> res,
                  absl::Span<const uint64_t> a, absl::Span<const uint64_t> b);

// res += (a * b)
void MultiplyAddPoly(const Params& params, absl::Span<uint64_t> res,
                     absl::Span<const uint64_t> a,
                     absl::Span<const uint64_t> b);
// res = a + b
void AddPoly(const Params& params, absl::Span<uint64_t> res,
             absl::Span<const uint64_t> a, absl::Span<const uint64_t> b);
// res += a
void AddPolyInto(const Params& params, absl::Span<const uint64_t> res,
                 absl::Span<const uint64_t> a);
// res = -a
void NegatePoly(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> a);

void AutomorphismPoly(const Params& params, absl::Span<uint64_t> res,
                      absl::Span<const uint64_t> a, size_t t);
void AutomorphismPolyUncrtd(const Params& params, absl::Span<uint64_t> res,
                            absl::Span<const uint64_t> a, size_t t);

// in is a PolyRaw, res is a RNS
void ReduceCopy(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> in);

void ReducePoly(const Params& params, absl::Span<uint64_t> res);

// matrix operator

PolyMatrixRaw Noise(const Params& params, size_t rows, size_t cols,
                    const DiscreteGaussian& dg,
                    yacl::crypto::Prg<uint64_t>& prg);

void GenTernaryMatrix(const Params& params, PolyMatrixRaw& mat, size_t hamming,
                      yacl::crypto::Prg<uint64_t>& prg);

PolyMatrixNtt ShiftRowsByOne(const PolyMatrixNtt& in);

PolyMatrixRaw Stack(const PolyMatrixRaw& a, const PolyMatrixRaw& b);
PolyMatrixNtt StackNtt(const PolyMatrixNtt& a, const PolyMatrixNtt& b);

// a is a 1x1 matrix, view as a scalar
void ScalarMultiply(const Params& params, PolyMatrixNtt& res,
                    const PolyMatrixNtt& a, const PolyMatrixNtt& b);
PolyMatrixNtt ScalarMultiply(const Params& params, const PolyMatrixNtt& a,
                             const PolyMatrixNtt& b);

void Multiply(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
              const PolyMatrixNtt& b);

PolyMatrixNtt Multiply(const Params& params, const PolyMatrixNtt& a,
                       const PolyMatrixNtt& b);

void MultiplyNoReduce(PolyMatrixNtt& res, const PolyMatrixNtt& a,
                      const PolyMatrixNtt& b, size_t start_inner_dim);

void Automorphism(const Params& params, PolyMatrixRaw& res,
                  const PolyMatrixRaw& a, size_t t);
PolyMatrixRaw Automorphism(const Params& params, const PolyMatrixRaw& a,
                           size_t t);
// res = a + b
void Add(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
         const PolyMatrixNtt& b);
PolyMatrixNtt Add(const Params& params, const PolyMatrixNtt& a,
                  const PolyMatrixNtt& b);

// res += a
void AddInto(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a);
void AddIntoAt(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
               size_t t_row, size_t t_col);

void Negate(const Params& params, PolyMatrixRaw& res, const PolyMatrixRaw& a);
PolyMatrixRaw Negate(const Params& params, const PolyMatrixRaw& a);

void FromNtt(const Params& params, PolyMatrixRaw& out, const PolyMatrixNtt& in);
PolyMatrixRaw FromNtt(const Params& params, const PolyMatrixNtt& in);
void FromNttScratch(const Params& params, PolyMatrixRaw& out,
                    absl::Span<uint64_t> scratch, const PolyMatrixNtt& in);

void ToNtt(const Params& params, PolyMatrixNtt& out, const PolyMatrixRaw& in);
PolyMatrixNtt ToNtt(const Params& params, const PolyMatrixRaw& in);
void ToNttNoReduce(const Params& params, PolyMatrixNtt& out,
                   const PolyMatrixRaw& in);

PolyMatrixRaw MatrixWithIdentity(const PolyMatrixRaw& p);

}  // namespace psi::spiral
