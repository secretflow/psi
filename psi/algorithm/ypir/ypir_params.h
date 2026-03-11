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

#include <cstdint>
#include <vector>

#include "psi/algorithm/ypir/ypir_util.h"

namespace psi::ypir::byhe {

class Secret {
 public:
  Secret() = default;
  Secret(uint64_t length, uint64_t cmod);

  uint64_t get_data_i(uint64_t idx) const { return data[idx]; }
  uint64_t get_len() const { return len_; }
  uint64_t get_mod() const { return mod_; }
  bool get_ntt_form() const { return is_ntt_; }

  void switch_ntt_format() { is_ntt_ = !is_ntt_; }

  std::vector<uint64_t> data;

 private:
  uint64_t len_ = 0;
  uint64_t mod_ = 0;
  bool is_ntt_ = false;
};

struct AutoParams {
  uint64_t b = 0;
  uint64_t z = 0;
  uint64_t t = 0;
};

struct DecompParams {
  uint64_t b = 0;
  uint64_t z = 0;
  uint64_t t = 0;
};

class FheParams {
 public:
  FheParams(uint64_t rlwe_degree, uint64_t rlwe_ct_modulus,
            uint64_t rlwe_pt_modulus, uint64_t lwe_dimension,
            uint64_t lwe_ct_modulus, uint64_t lwe_pt_modulus, double sigma,
            double sigma_ring, AutoParams auto_params,
            DecompParams decomp_params);

  // byhe-style getters
  uint64_t get_poly_degree() const { return rlwe_degree_; }
  uint64_t get_rlwe_cmod() const { return rlwe_ct_modulus_; }
  uint64_t get_rlwe_pmod() const { return rlwe_pt_modulus_; }
  uint64_t get_lwe_dimension() const { return lwe_dimension_; }
  uint64_t get_lwe_cmod() const { return lwe_ct_modulus_; }
  uint64_t get_lwe_pmod() const { return lwe_pt_modulus_; }
  uint64_t get_b_auto() const { return auto_params_.b; }
  uint64_t get_z_auto() const { return auto_params_.z; }
  uint64_t get_t_auto() const { return auto_params_.t; }
  uint64_t get_b_decomp() const { return decomp_params_.b; }
  uint64_t get_z_decomp() const { return decomp_params_.z; }
  uint64_t get_t_decomp() const { return decomp_params_.t; }
  double get_sig() const { return sigma_; }
  double get_sig_ring() const { return sigma_ring_; }
  ByheHexlNtt& get_ntt() const noexcept { return ntt_; }

  const std::vector<std::vector<uint64_t>>& get_persudo_matrix_simplepir()
      const noexcept {
    return persudo_matrix_simplepir_;
  }
  const std::vector<std::vector<uint64_t>>& get_persudo_matrix_doublepir()
      const noexcept {
    return persudo_matrix_doublepir_;
  }
  const std::vector<std::vector<std::vector<uint64_t>>>&
  get_persudo_hcube_ypir() const noexcept {
    return persudo_hcube_ypir_;
  }
  const uint64_t* get_persudo_matrix_simplepir_flat() const noexcept {
    return persudo_matrix_simplepir_flat_.data();
  }
  const uint64_t* get_persudo_matrix_doublepir_flat() const noexcept {
    return persudo_matrix_doublepir_flat_.data();
  }

  void set_persudo_matrix_simplepir(uint64_t row);
  void set_persudo_matrix_doublepir(uint64_t row);
  void set_persudo_hcube_ypir();
  void set_automap(std::vector<uint64_t>& idx);
  const std::vector<uint32_t>& get_automap(uint64_t idx) const noexcept {
    return automap_[idx];
  }
  void set_precomputed_pt(uint64_t max_lh);
  const std::vector<uint64_t>& get_precomputed_pt(uint64_t lh) const noexcept {
    return precomputed_pt_[lh];
  }

  void SetNttForward(NttForwardFn fn) { ntt_forward_ = fn; }

 private:
  uint64_t rlwe_degree_ = 0;
  uint64_t rlwe_ct_modulus_ = 0;
  uint64_t rlwe_pt_modulus_ = 0;

  uint64_t lwe_dimension_ = 0;
  uint64_t lwe_ct_modulus_ = 0;
  uint64_t lwe_pt_modulus_ = 0;

  double sigma_ = 0.0;
  double sigma_ring_ = 0.0;

  AutoParams auto_params_;
  DecompParams decomp_params_;

  uint64_t rlwe_degree_log2_ = 0;
  mutable ByheHexlNtt ntt_;

  std::vector<std::vector<uint64_t>> persudo_matrix_simplepir_;
  std::vector<std::vector<uint64_t>> persudo_matrix_doublepir_;
  std::vector<std::vector<std::vector<uint64_t>>> persudo_hcube_ypir_;
  std::vector<uint64_t> persudo_matrix_simplepir_flat_;
  std::vector<uint64_t> persudo_matrix_doublepir_flat_;
  std::vector<std::vector<uint32_t>> automap_;
  std::vector<std::vector<uint64_t>> precomputed_pt_;
  AESCTR_PRNG prg_;
  NttForwardFn ntt_forward_ = nullptr;
};

class PirParams {
 public:
  PirParams(uint64_t rows, uint64_t cols);

  uint64_t get_row() const { return rows_; }
  uint64_t get_col() const { return cols_; }

 private:
  uint64_t rows_ = 0;
  uint64_t cols_ = 0;
};

}  // namespace psi::ypir::byhe
