// Copyright 2023 Ant Group Co., Ltd.
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

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "libdivide.h"
#include "yacl/utils/platform_utils.h"

#include "psi/rr22/okvs/dense_mtx.h"
#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/okvs/paxos_hash.h"
#include "psi/rr22/okvs/paxos_utils.h"

namespace psi::rr22::okvs {

struct PaxosParam {
  // the type of dense columns.
  enum class DenseType { Binary, GF128 };

  uint64_t sparse_size = 0;
  uint64_t dense_size = 0;
  uint64_t weight = 0;
  uint64_t g = 0;
  uint64_t ssp = 40;
  DenseType dt = DenseType::GF128;

  PaxosParam() = default;
  PaxosParam(const PaxosParam&) = default;
  PaxosParam& operator=(const PaxosParam&) = default;

  PaxosParam(uint64_t num_items, uint64_t weight = 3, uint64_t ssp = 40,
             DenseType dt = DenseType::GF128) {
    Init(num_items, weight, ssp, dt);
  }

  // computes the paxos parameters based the parameters.
  void Init(uint64_t num_items, uint64_t weight = 3, uint64_t ssp = 40,
            DenseType dt = DenseType::GF128);

  // the size of the paxos data structure.
  uint64_t size() const { return sparse_size + dense_size; }
};

// [RR22] Blazing Fast PSI from Improved OKVS and Subfield VOLE, CCS 2022
// https://eprint.iacr.org/2022/320
// okvs code reference https://github.com/Visa-Research/volepsi
// The core Paxos algorithm. The template parameter
// IdxType should be in {u8,u16,u32,u64} and large
// enough to fit the paxos size value.
template <typename IdxType>
class Paxos : public PaxosParam {
 public:
  Paxos() = default;
  Paxos(const Paxos&) = default;
  Paxos(Paxos&&) = default;
  Paxos& operator=(const Paxos&) = default;
  Paxos& operator=(Paxos&&) = default;

  // initialize the paxos with the given parameters.
  void Init(uint64_t num_items, uint64_t weight, uint64_t ssp,
            PaxosParam::DenseType dt, uint128_t seed) {
    PaxosParam p(num_items, weight, ssp, dt);
    Init(num_items, p, seed);
  }

  // initialize the paxos with the given parameters.
  void Init(uint64_t num_items, PaxosParam p, uint128_t seed);

  // set the input keys which define the paxos matrix. After that,
  // encode can be called more than once.
  void SetInput(absl::Span<const uint128_t> inputs);

  void SetInput(MatrixView<IdxType> rows, absl::Span<uint128_t> dense,
                absl::Span<absl::Span<IdxType>> cols,
                absl::Span<IdxType> col_backing,
                absl::Span<IdxType> col_weights);

  void Encode(
      absl::Span<uint128_t> values, absl::Span<uint128_t> output,
      const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng = nullptr) {
    PxVector V(values);
    PxVector P(output);
    auto h = P.DefaultHelper();
    Encode(V, P, h, prng);
  }

  // encode the given input with the given paxos p. Vec and ConstVec should
  // meet the PxVector concept... Helper used to perform operations on values.
  void Encode(
      const PxVector& values, PxVector& output, PxVector::Helper& h,
      const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng = nullptr);

  // Decode the given input based on the data paxos structure p. The
  // output is written to values.
  void Decode(absl::Span<const uint128_t> input, absl::Span<uint128_t> values,
              absl::Span<uint128_t> p);

  void Decode(absl::Span<const uint128_t> inputs, PxVector& values,
              const PxVector& PP, PxVector::Helper& h);

  // decodes 32 instances. rows should contain the row indicies, dense the dense
  // part. values is where the values are written to. p is the Paxos, h is the
  // value op. helper.
  void Decode32(absl::Span<IdxType> rows_span, absl::Span<uint128_t> dense_span,
                absl::Span<uint128_t> values_span, const PxVector& p,
                const PxVector::Helper& h);

  // decodes one instances. rows should contain the row indicies, dense the
  // dense part. values is where the values are written to. p is the Paxos, h is
  // the value op. helper.
  void Decode1(absl::Span<IdxType> rows, const uint128_t dense,
               uint128_t* values, const PxVector& p, const PxVector::Helper& h);

  // A sparse representation of the F * C^-1 matrix.
  struct FCInv {
    explicit FCInv(uint64_t n) : mtx(n) {}
    std::vector<std::vector<IdxType>> mtx;
  };

  // the method for generating the row data based on the input value.
  PaxosHash<IdxType> hasher_;

 private:
  // helper function that generates the column data given that
  // the row data has been populated (via setInput(...)).
  void RebuildColumns(absl::Span<IdxType> col_weights, uint64_t total_weight);

  // perform the tringulization algorithm for encoding. This
  // populates mainRows,mainCols with the rows/columns of C
  // gapRows are all the rows that are in the gap.
  void Triangulate(std::vector<IdxType>& main_rows,
                   std::vector<IdxType>& main_cols,
                   std::vector<std::array<IdxType, 2>>& gap_rows);

  // once triangulated, this is used to assign values
  // to output (paxos).
  void Backfill(absl::Span<IdxType> main_rows, absl::Span<IdxType> main_cols,
                absl::Span<std::array<IdxType, 2>> gap_rows,
                const PxVector& values, PxVector& output, PxVector::Helper& h,
                const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng);

  // once triangulated, this is used to assign values
  // to output (paxos). Use the gf128 dense algorithm.
  void BackfillGf128(absl::Span<IdxType> main_rows,
                     absl::Span<IdxType> main_cols,
                     absl::Span<std::array<IdxType, 2>> gap_rows,
                     const PxVector& values, PxVector& output,
                     PxVector::Helper& h,
                     const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng);

  // once triangulated, this is used to assign values
  // to output (paxos). Use the classic binary dense algorithm.
  void BackfillBinary(absl::Span<IdxType> main_rows,
                      absl::Span<IdxType> main_cols,
                      absl::Span<std::array<IdxType, 2>> gap_rows,
                      const PxVector& values, PxVector& output,
                      PxVector::Helper& h,
                      const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng);

  // returns the sparse representation of the F * C^-1 matrix.
  FCInv GetFCInv(absl::Span<IdxType> main_rows, absl::Span<IdxType> main_cols,
                 absl::Span<std::array<IdxType, 2>> gap_rows) const;

  // returns which columns are used for the gap. This
  // is only used for binary dense method.
  std::vector<uint64_t> GetGapCols(
      const FCInv& fcinv, absl::Span<std::array<IdxType, 2>> gap_rows) const;

  // returns x2' = x2 - D' r - FC^-1 x1
  PxVector GetX2Prime(const FCInv& fcinv,
                      absl::Span<std::array<IdxType, 2>> gap_rows,
                      absl::Span<uint64_t> gap_cols, const PxVector& X,
                      const PxVector& P, PxVector::Helper& h);

  // returns E' = -FC^-1B + E
  DenseMtx GetEPrime(const FCInv& fcinv,
                     absl::Span<std::array<IdxType, 2>> gap_rows,
                     absl::Span<uint64_t> gap_cols);

  void RandomizeDenseCols(
      PxVector&, PxVector::Helper&, absl::Span<uint64_t> gap_cols,
      const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng);

  // the number of items to be encoded.
  IdxType num_items_ = 0;

  // the encoding/decoding seed.
  uint128_t seed_ = 0;

  // The dense part of the paxos matrix
  std::vector<uint128_t> dense_;

  // matrix number_items * weight
  std::vector<IdxType> rows_;

  // the sparse columns of the matrix
  std::vector<absl::Span<IdxType>> cols_;

  // the memory used to store the column data.
  std::vector<IdxType> col_backing_;

  // A data structure used to track the current weight of the rows.s
  WeightData<IdxType> weight_sets_;

  // when decoding, add the decoded value to the
  // output, as opposed to overwriting.
  bool add_to_decode_ = false;
};

}  // namespace psi::rr22::okvs
