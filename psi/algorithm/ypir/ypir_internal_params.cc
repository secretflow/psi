#include "psi/algorithm/ypir/ypir_internal_params.h"

#include <vector>

#include "yacl/base/exception.h"

namespace psi::ypir::internal::ypir {
namespace {

thread_local psi::ypir::ypir_internal::YpirHexlNtt* g_ntt = nullptr;

void NttForwardWrapper(uint64_t* data, size_t len) {
  g_ntt->Forward(data, len);
}

Context BuildContext(uint64_t rows, uint64_t cols) {
  using namespace psi::ypir::ypir_internal;

  const uint64_t lwe_dimension = 1024;
  const uint64_t lwe_pmod = (1 << 8);
  const uint64_t lwe_cmod = (1ULL << 32);
  const uint64_t rlwe_degree = 2048;
  const uint64_t rlwe_pmod = (1 << 16);
  const uint64_t rlwe_cmod = kCrtMod;
  const double sigma = 3.19;
  const double sigma_ring = 3.19;
  const AutoParams auto_params{20, 18, 2};
  const DecompParams decomp_params{16, 16, 1};

  auto fhe_params = std::make_shared<FheParams>(
      rlwe_degree, rlwe_cmod, rlwe_pmod, lwe_dimension, lwe_cmod, lwe_pmod,
      sigma, sigma_ring, auto_params, decomp_params);
  auto pir_params = std::make_shared<PirParams>(rows, cols);
  auto prng = std::make_shared<AESCTR_PRNG>();

  g_ntt = &fhe_params->get_ntt();
  fhe_params->SetNttForward(&NttForwardWrapper);
  fhe_params->set_persudo_matrix_simplepir(pir_params->get_col());
  fhe_params->set_persudo_matrix_doublepir(pir_params->get_row());
  fhe_params->set_persudo_hcube_ypir();

  const uint64_t expo = GetLog2(rlwe_degree);
  std::vector<uint64_t> auto_idx;
  for (uint64_t i = 1; i <= expo; ++i) {
    auto_idx.push_back((1ULL << i) + 1);
  }
  fhe_params->set_automap(auto_idx);
  fhe_params->set_precomputed_pt(expo);

  return {std::move(fhe_params), std::move(pir_params), std::move(prng)};
}

}  // namespace

Context CreateContext(const YpirParameters& params) {
  YACL_ENFORCE(params.mode == YpirMode::kDoublepir);
  return BuildContext(params.db_rows, params.db_cols);
}

}  // namespace psi::ypir::internal::ypir
