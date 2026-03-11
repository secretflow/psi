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

#include "psi/algorithm/spiral/params.h"

#include <string>

#include "spdlog/spdlog.h"
#include "yacl/crypto/hash/blake3.h"

#include "psi/algorithm/spiral/arith/arith.h"

namespace psi::spiral {

Params::Params(std::size_t poly_len, std::vector<std::uint64_t> moduli,
               double noise_width, PolyMatrixParams poly_matrix_params,
               QueryParams query_params)
    : poly_len_(poly_len),
      noise_width_(noise_width),
      poly_matrix_params_(std::move(poly_matrix_params)),
      query_params_(std::move(query_params)) {
  YACL_ENFORCE(poly_matrix_params_.q2_bits_ >= kMinQ2Bits);

  poly_len_log2_ = arith::Log2(poly_len_);

  std::size_t crt_count = moduli.size();
  YACL_ENFORCE(crt_count <= kMaxModuli,
               "Current Spiral implementation support moduli size should be "
               "less than {}",
               kMaxModuli);

  std::vector<seal::Modulus> moduli_seal;
  for (std::size_t i = 0; i < crt_count; ++i) {
    moduli_seal.emplace_back(moduli[i]);
  }

  // build ntt tables
  ntt_tables_ = arith::BuildNttTables(poly_len, moduli_seal);

  std::uint64_t modulus{1ULL};
  for (const auto& m : moduli) {
    modulus *= m;
  }
  seal::Modulus modulus_seal(modulus);

  std::uint64_t modulus_log2 = arith::Log2Ceil(modulus);

  // for crt resconstruct
  std::uint64_t mod0_inv_mod1 = 0ULL;
  std::uint64_t mod1_inv_mod0 = 0ULL;
  // here implicity this code only support crt_count = 2
  if (crt_count == 2) {
    mod0_inv_mod1 = moduli[0] * arith::InvertUintMod(moduli[0], moduli[1]);
    mod1_inv_mod0 = moduli[1] * arith::InvertUintMod(moduli[1], moduli[0]);
  }

  // now we can construct other parameters
  crt_params_ =
      CrtParams(crt_count, mod0_inv_mod1, mod1_inv_mod0, std::move(moduli_seal),
                std::move(modulus_seal), modulus_log2);

  ComputeId();
}

Params Params::ParamsWithModuli(const Params& params,
                                std::vector<uint64_t> moduli) {
  return Params(params.PolyLen(), std::move(moduli), params.noise_width_,
                params.poly_matrix_params_, params.query_params_);
}

void Params::ComputeId() {
  yacl::crypto::Blake3Hash hasher;
  auto digest = hasher.Update(this->ToString()).CumulativeHash();
  std::memcpy(&id_, digest.data(), sizeof(uint64_t));
}

std::size_t Params::ElementSizeOfPt(size_t element_byte_len) const {
  // one element needs how many coeffs
  size_t coeff_size_of_element =
      arith::UintNum(8 * element_byte_len, PtModulusBitLen());

  // we have poly_len coeffs, so we can hold xx many element
  size_t element_size_of_pt = PtCoeffs() / coeff_size_of_element;

  // at least, one plaintext must hold one element
  element_size_of_pt = std::max<size_t>(element_size_of_pt, 1u);

  return element_size_of_pt;
}

size_t Params::UpdateByDatabaseInfo(const DatabaseMetaInfo& database_info) {
  SPDLOG_INFO("rows: {}, byte_per_row: {}", database_info.rows_,
              database_info.byte_size_per_row_);
  // here, we only consider one row of raw data can be holded by one plaintext
  // in SpiralPIR

  // if the byte_size_per_row_ > MaxByteLenOfPt, we use the min
  size_t element_byte_len =
      std::min(database_info.byte_size_per_row_, MaxByteLenOfPt());

  // the number of one plaintext can hold the rows in raw database
  size_t element_size_of_pt = ElementSizeOfPt(element_byte_len);

  // we can conmpute how many plaintexts can be used to hold the whole raw
  // database
  size_t plaintext_size =
      arith::UintNum(database_info.rows_, element_size_of_pt);

  SPDLOG_INFO("total pt size: {}", plaintext_size);

  size_t v1 = 0;
  size_t v2 = 0;
  // now we need to adjust the v1 & v2 to satisfy 2^(v1 + v2) >= plaintext
  if (plaintext_size <= 4) {
    v1 = 2;
    v2 = 1;
    // reduce t_gsw to satisfy related conditions
    poly_matrix_params_.t_gsw_ = 4;
  } else {
    uint64_t log2 = arith::Log2Ceil(static_cast<uint64_t>(plaintext_size));
    v1 = static_cast<size_t>(log2 * 0.6);
    v2 = static_cast<size_t>(log2 * 0.4);
    // double check
    while (static_cast<size_t>(1 << (v1 + v2)) < plaintext_size) {
      v2 += 1;
    }
  }
  // set v1 and v2
  SetDbDim1(v1);
  SetDbDim2(v2);

  ValidCheck();

  // reset id
  ComputeId();

  return plaintext_size;
}

std::uint64_t Params::CrtCompose2(std::uint64_t x, std::uint64_t y) const {
  YACL_ENFORCE(crt_params_.crt_count_ == 2);

  uint128_t x_128 = yacl::MakeUint128(0, x);
  uint128_t y_128 = yacl::MakeUint128(0, y);
  auto val = x_128 * yacl::MakeUint128(0, crt_params_.mod1_inv_mod0_);

  val += (y_128 * yacl::MakeUint128(0, crt_params_.mod0_inv_mod1_));

  // reduce
  return arith::BarrettReductionU128Raw(val, BarrettCr0Modulus(),
                                        BarrettCr1Modulus(),
                                        crt_params_.modulus_.value());
}

// TODO: optimize memory copy span?
std::uint64_t Params::CrtCompose(const std::vector<std::uint64_t>& a,
                                 std::size_t idx) const {
  if (crt_params_.crt_count_ == 1) {
    return CrtCompose1(a[idx]);
  } else {
    return CrtCompose2(a[idx], a[idx + poly_len_]);
  }
}

[[nodiscard]] std::string Params::ToString() {
  std::ostringstream ss;

  ss << "poly_len: " << poly_len_;
  ss << ", Pt dimension: " << N() << ", pt modulus: " << PtModulus() << "\n";

  ss << "modulus: " << Modulus() << ", moduli: {";
  for (size_t i = 0; i < CrtCount(); ++i) {
    ss << Moduli(i) << (i + 1 == CrtCount() ? "" : ", ");
  }
  ss << "}\n";

  ss << "t_conv: " << TConv() << ", t_exp_left: " << TExpLeft()
     << ", t_exp_right: " << TExpRight() << ", t_gsw: " << TGsw() << "\n";

  ss << "v1: " << DbDim1() << ", v2: " << DbDim2();

  return ss.str();
}

size_t Params::Rho() const {
  const size_t q2_bits = 28;
  const size_t lwe_q_prime_bits = q2_bits;

  const size_t pt_bits = PtModulusBitLen();
  const double blowup_factor =
      static_cast<double>(lwe_q_prime_bits) / static_cast<double>(pt_bits);
  const double num_polys_needed =
      (blowup_factor * static_cast<double>(1024 + 1)) /
      static_cast<double>(PolyLen());

  const size_t smaller_params_db_dim_2 =
      static_cast<size_t>(std::ceil(std::log2(num_polys_needed)));

  const size_t rho = 1ULL << smaller_params_db_dim_2;

  return rho;
}

}  // namespace psi::spiral
