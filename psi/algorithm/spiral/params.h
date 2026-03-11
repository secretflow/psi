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

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "seal/modulus.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/base/int128.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/spiral/arith/arith.h"
#include "psi/algorithm/spiral/arith/ntt_table.h"
#include "psi/algorithm/spiral/arith/number_theory.h"

namespace psi::spiral {

// Use ParamsId to uniquely label a Params object
using ParamsId = std::uint64_t;

struct DatabaseMetaInfo {
  size_t rows_ = 0;
  // byte length of each row
  size_t byte_size_per_row_ = 0;

  DatabaseMetaInfo() = default;

  DatabaseMetaInfo(size_t rows, size_t byte_size_per_row)
      : rows_(rows), byte_size_per_row_(byte_size_per_row) {}
};

struct CrtParams {
  // RNS moduli num
  std::size_t crt_count_ = 0;

  // for crt reconstruct
  std::uint64_t mod0_inv_mod1_ = 0;

  // for crt reconstruct
  std::uint64_t mod1_inv_mod0_ = 0;

  // RNS moduli: qi
  std::vector<seal::Modulus> moduli_;

  // moduls q = q1 * q2
  seal::Modulus modulus_;

  // log2(q)
  std::uint64_t modulus_log2_ = 0;

  CrtParams() = default;

  CrtParams(std::size_t crt_count, std::uint64_t mod0_inv_mod1,
            std::uint64_t mod1_inv_mod0, std::vector<seal::Modulus> moduli,
            seal::Modulus modulus, std::uint64_t modulus_log2)
      : crt_count_(crt_count),
        mod0_inv_mod1_(mod0_inv_mod1),
        mod1_inv_mod0_(mod1_inv_mod0),
        moduli_(std::move(moduli)),
        modulus_(std::move(modulus)),
        modulus_log2_(modulus_log2) {}
};

struct PolyMatrixParams {
  // plaintext dimension, default value is 2
  std::size_t n_ = 2;

  // plaintext modulus p
  std::uint64_t pt_modulus_ = 256;

  // q2 used in modulus switching for response
  std::uint64_t q2_bits_ = 20;

  // z_conv in Spiral paper, used to genedate Gadget Matrix
  // t_Conv = log_{z_Conv] q} + 1
  std::size_t t_conv_ = 4;

  // t_{expLeft} = log_{z_{expLeft}} + 1, given t, then we can calculate z
  std::size_t t_exp_left_ = 8;

  // t_{expRight} = log_{z_{expRight}} + 1
  std::size_t t_exp_right_ = 8;

  // t_{GSW} = log_{z_{GSW} q} + 1
  std::size_t t_gsw_ = 6;

  PolyMatrixParams() = default;

  PolyMatrixParams(std::size_t n, std::uint64_t pt_modulus,
                   std::uint64_t q2_bits, std::size_t t_conv,
                   std::size_t t_exp_left, std::size_t t_exp_right,
                   std::size_t t_gsw)
      : n_(n),
        pt_modulus_(pt_modulus),
        q2_bits_(q2_bits),
        t_conv_(t_conv),
        t_exp_left_(t_exp_left),
        t_exp_right_(t_exp_right),
        t_gsw_(t_gsw) {}
};

struct QueryParams {
  // use the rng_pub related seed to compress the SpiralQuery size
  bool query_seed_compressed_ = true;

  // database shape: v1
  std::size_t db_dim1_ = 1;

  // database shape: v2
  std::size_t db_dim2_ = 1;

  // the number of sub-database, T
  // in our kernel level, we only process the case T = 1
  std::size_t instances_ = 1;

  QueryParams() = default;

  QueryParams(std::size_t db_dim_1, std::size_t db_dim_2, std::size_t instances,
              bool query_seed_compressed = true)
      : query_seed_compressed_(query_seed_compressed),
        db_dim1_(db_dim_1),
        db_dim2_(db_dim_2),
        instances_(instances) {}
};

class Params {
 public:
  Params(std::size_t poly_len, std::vector<std::uint64_t> moduli,
         double noise_width, PolyMatrixParams poly_matrix_params,
         QueryParams query_params);

  Params() = default;

  Params(const Params& other) = default;
  Params& operator=(const Params& other) = default;

  Params(Params&& other) = default;
  Params& operator=(Params&& other) = default;

  static Params ParamsWithModuli(const Params& params,
                                 std::vector<uint64_t> moduli);

  bool operator==(const Params& other) const { return id_ == other.id_; }

  bool operator!=(const Params& other) const { return !(*this == other); }

  size_t UpdateByDatabaseInfo(const DatabaseMetaInfo& database_info);

  void ValidCheck() const {
    YACL_ENFORCE(DbDim1() <= kMaxDbDim, "v1({}) should <= {}", DbDim1(),
                 kMaxDbDim);
    YACL_ENFORCE(DbDim2() <= kMaxDbDim, "v2({}) should <= {}", DbDim2(),
                 kMaxDbDim);

    YACL_ENFORCE(G() <= poly_len_log2_, "G({}) should <= log2d({})", G(),
                 poly_len_log2_);

    // v.size() = 2^g,  g = ceil (log2( 2^v1 + v2 * t_gsw ))
    // v.size() / 2 >= 2^v1
    size_t g_half = 1u << (G() - 1);
    YACL_ENFORCE(g_half >= (1u << DbDim1()), "G/2 = {} should >= 2^{} = {}",
                 g_half, DbDim1(), (1u << DbDim1()));

    // v.size() / 2 >= v2 * t_gsw
    YACL_ENFORCE(g_half >= (DbDim2() * TGsw()),
                 "G/2({}) should >= v2({}) * t_gsw({}) = {}", g_half, DbDim2(),
                 TGsw(), DbDim2() * TGsw());
  }

  [[nodiscard]] const std::vector<std::uint64_t>& GetNttForwardTable(
      std::size_t i) const {
    return ntt_tables_[i][0];
  }

  [[nodiscard]] const std::vector<std::uint64_t>& GetNttForwardPrimeTable(
      std::size_t i) const {
    return ntt_tables_[i][1];
  }

  [[nodiscard]] const std::vector<std::uint64_t>& GetNttInverseTable(
      std::size_t i) const {
    return ntt_tables_[i][2];
  }
  [[nodiscard]] const std::vector<std::uint64_t>& GetNttInversePrimeTable(
      std::size_t i) const {
    return ntt_tables_[i][3];
  }

  [[nodiscard]] std::pair<std::size_t, std::size_t> GetSkGswDims() const {
    return {poly_matrix_params_.n_, 1};
  };

  [[nodiscard]] uint64_t Moduli(size_t c) const {
    return crt_params_.moduli_[c].value();
  }

  uint64_t Moduli(size_t c) { return crt_params_.moduli_[c].value(); }

  [[nodiscard]] const seal::Modulus& ModuliSeal(size_t c) const {
    return crt_params_.moduli_[c];
  }

  [[nodiscard]] std::pair<std::size_t, std::size_t> GetSkRegDims() const {
    return {1, 1};
  }

  [[nodiscard]] std::size_t NumExpanded() const {
    return static_cast<size_t>(1) << query_params_.db_dim1_;
  }

  [[nodiscard]] std::size_t NumItems() const {
    return (static_cast<size_t>(1) << query_params_.db_dim1_) *
           (static_cast<size_t>(1) << query_params_.db_dim2_);
  }

  [[nodiscard]] std::size_t ItemSize() const {
    auto log_p = arith::Log2(poly_matrix_params_.pt_modulus_);

    return query_params_.instances_ * poly_matrix_params_.n_ *
           poly_matrix_params_.n_ * poly_len_ * log_p / 8;
  }

  [[nodiscard]] std::size_t G() const {
    // v_2 * t_{GSW} + 2^{v_1}, should <= d
    auto num_bits_to_gen =
        poly_matrix_params_.t_gsw_ * query_params_.db_dim2_ + NumExpanded();

    return static_cast<std::size_t>(arith::Log2Ceil(num_bits_to_gen));
  }

  [[nodiscard]] std::size_t StopRound() const {
    return static_cast<std::size_t>(
        arith::Log2Ceil(poly_matrix_params_.t_gsw_ * query_params_.db_dim2_));
  }

  [[nodiscard]] uint64_t ScaleK() const {
    return crt_params_.modulus_.value() / poly_matrix_params_.pt_modulus_;
  }

  [[nodiscard]] size_t PtCoeffs() const {
    return poly_matrix_params_.n_ * poly_matrix_params_.n_ * poly_len_;
  }

  [[nodiscard]] size_t N() const { return poly_matrix_params_.n_; };

  [[nodiscard]] size_t Q2Bits() const { return poly_matrix_params_.q2_bits_; };

  [[nodiscard]] size_t Instances() const { return query_params_.instances_; };

  [[nodiscard]] uint64_t TConv() const { return poly_matrix_params_.t_conv_; }
  [[nodiscard]] uint64_t TGsw() const { return poly_matrix_params_.t_gsw_; }
  [[nodiscard]] uint64_t TExpLeft() const {
    return poly_matrix_params_.t_exp_left_;
  }
  [[nodiscard]] uint64_t TExpRight() const {
    return poly_matrix_params_.t_exp_right_;
  }

  [[nodiscard]] size_t CrtCount() const { return crt_params_.crt_count_; }

  [[nodiscard]] size_t DbDim2() const { return query_params_.db_dim2_; }

  [[nodiscard]] size_t DbDim1() const { return query_params_.db_dim1_; }

  [[nodiscard]] uint64_t Modulus() const {
    return crt_params_.modulus_.value();
  }

  [[nodiscard]] const seal::Modulus& ModulusSeal() const {
    return crt_params_.modulus_;
  }

  [[nodiscard]] uint64_t PtModulus() const {
    return poly_matrix_params_.pt_modulus_;
  }

  [[nodiscard]] std::size_t FactorOnFirstDim() const {
    if (query_params_.db_dim2_ == 0) {
      return 1;
    } else {
      return 2;
    }
  }

  [[nodiscard]] std::uint64_t CrtCompose1(std::uint64_t x) const {
    YACL_ENFORCE(crt_params_.crt_count_ == 1);
    return x;
  }

  [[nodiscard]] std::uint64_t CrtCompose2(std::uint64_t x,
                                          std::uint64_t y) const;

  std::uint64_t CrtCompose(const std::vector<std::uint64_t>& a,
                           std::size_t idx) const;
  uint64_t GetQPrime1() const { return 1 << 20; }

  uint64_t GetQPrime2() const {
    if (Q2Bits() == ModulusLog2()) {
      return Modulus();
    } else {
      return kQ2Values[Q2Bits()];
    }
  }
  // other util methods

  [[nodiscard]] std::string ToString();

  [[nodiscard]] std::uint64_t PtModulusBitLen() const {
    return arith::Log2(poly_matrix_params_.pt_modulus_);
  }

  // the number of raw data rows that one Pt can hold
  std::size_t ElementSizeOfPt(size_t element_byte_len) const;

  static std::size_t ElementSizeOfPt(size_t element_byte_len,
                                     size_t pt_modulus_bit_len,
                                     size_t pt_coeffs) {
    // one element needs how many coeffs
    size_t coeff_size_of_element =
        arith::UintNum(8 * element_byte_len, pt_modulus_bit_len);

    // we have poly_len coeffs, so we can hold xx many element
    size_t element_size_of_pt = pt_coeffs / coeff_size_of_element;

    // at least, one plaintext must hold one element
    YACL_ENFORCE_GT(element_size_of_pt, static_cast<size_t>(0));

    return element_size_of_pt;
  }

  // one Pt can hold how many bytes
  // in spiral, one Pt is Rp^{n*n}
  std::size_t MaxByteLenOfPt() const {
    size_t max_bits = poly_len_ * N() * N() * PtModulusBitLen();
    // floor
    return max_bits / 8;
  }

  // one Pt can hold how many bits
  std::size_t MaxBitLenOfPt() const {
    return poly_len_ * N() * N() * PtModulusBitLen();
  }

  std::size_t PolyLen() const { return poly_len_; }

  std::size_t PolyLenLog2() const { return poly_len_log2_; }

  std::uint64_t ModulusLog2() const { return crt_params_.modulus_log2_; }

  // const ratio 1 in Barrett Reduce, for large modulus
  std::uint64_t BarrettCr1Modulus() const {
    return crt_params_.modulus_.const_ratio()[1];
  }

  // const ratio 0 in Barrett Reduce, for large modulus
  std::uint64_t BarrettCr0Modulus() const {
    return crt_params_.modulus_.const_ratio()[0];
  };

  // const ratio 0 in Barrett Reduce, for ecah moduli
  std::uint64_t BarrettCr1(size_t i) const {
    return crt_params_.moduli_[i].const_ratio()[1];
  }

  // const ratio 1 in Barrett Reduce, for ecah moduli
  std::uint64_t BarrettCr0(size_t i) const {
    return crt_params_.moduli_[i].const_ratio()[0];
  }

  double NoiseWidth() const { return noise_width_; }

  std::uint64_t Mod0InvMod1() const { return crt_params_.mod0_inv_mod1_; }

  std::uint64_t Mod1InvMod0() const { return crt_params_.mod1_inv_mod0_; }

  ParamsId Id() const { return id_; }

  size_t DbRowsPadded() const {
    return 1 << (query_params_.db_dim1_ + poly_len_log2_);
  }

  size_t Rho() const;
  void SetDbDim1(size_t v1) { query_params_.db_dim1_ = v1; }
  void SetDbDim2(size_t v2) { query_params_.db_dim2_ = v2; }

 private:
  void ComputeId();

  // d in R = Z[x]/x^d + 1
  std::size_t poly_len_ = 0;

  // log2(d)
  std::size_t poly_len_log2_ = 0;

  // Ntt tables for given RNS moduli
  arith::NttTables ntt_tables_;

  // crt params for RNS related computation required
  CrtParams crt_params_;

  // noise for Discrete Gaussian
  double noise_width_ = 6.4;

  // PolyMatrix related params
  PolyMatrixParams poly_matrix_params_;

  // Query related params
  QueryParams query_params_;

  ParamsId id_ = 0;
};

}  // namespace psi::spiral
