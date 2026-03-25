#include "psi/algorithm/ypir/legacy/server.h"

#include <vector>

namespace psi::ypir::ypir_internal {
namespace {

void DoublepirAnswer(const uint8_t* db, uint32_t* qu0,
                     std::vector<uint64_t>& qu1,
                     const std::vector<std::vector<uint64_t>>& server_hint,
                     std::vector<uint64_t>& h2,
                     std::vector<std::vector<uint64_t>>& h3,
                     std::vector<uint64_t>& h4, const FheParams& fparm,
                     const PirParams& pparm) {
  const uint64_t row = pparm.get_row();
  const uint64_t col = pparm.get_col();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();
  const uint64_t b = fparm.get_b_decomp();
  const uint64_t z = fparm.get_z_decomp();
  const uint64_t t = fparm.get_t_decomp();

  std::vector<uint32_t> simple_res(row, 0);
  MatVecU8U32Mod2p32(db, qu0, simple_res.data(), row, col);

  std::vector<std::vector<uint16_t>> trans_simple_res_decomp;
  VectorColDecompose(simple_res, trans_simple_res_decomp, b, z, t);

  const uint64_t* matrix_d2_flat = fparm.get_persudo_matrix_doublepir_flat();
  const uint64_t poly_degree = fparm.get_poly_degree();

  MatrixVectorFirstDimension(h2, server_hint, qu1, rlwe_cmod);

  MatrixMultiplicationFlatU16(h3, trans_simple_res_decomp, matrix_d2_flat,
                              poly_degree, rlwe_cmod);

  MatrixVectorMultiplicationU16(h4, trans_simple_res_decomp, qu1, rlwe_cmod);
}

}  // namespace

void YpirHintGenerate(std::vector<std::vector<uint64_t>>& db,
                      std::vector<uint64_t>& h0,
                      std::vector<std::vector<uint64_t>>& double_server_hint,
                      std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
                      AESCTR_PRNG& prng,
                      const FheParams& fparm, const PirParams& pparm) {
  const uint64_t lwe_dimension = fparm.get_lwe_dimension();
  const uint64_t poly_degree = fparm.get_poly_degree();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();
  const uint64_t t_auto = fparm.get_t_auto();
  const uint64_t t_decomp = fparm.get_t_decomp();
  const uint64_t expo = GetLog2(poly_degree);
  const uint64_t pack_num = lwe_dimension * t_decomp;

  std::vector<std::vector<uint64_t>> double_client_hint(
      lwe_dimension * t_decomp, std::vector<uint64_t>(poly_degree, 0));

  const uint64_t* matrix_flat = fparm.get_persudo_matrix_simplepir_flat();
  std::vector<std::vector<uint64_t>> simple_hint(
      pparm.get_row(), std::vector<uint64_t>(lwe_dimension, 0));
  MatrixMultiplicationFlat(simple_hint, db, matrix_flat, lwe_dimension,
                           fparm.get_lwe_cmod());

  std::vector<std::vector<uint64_t>> matrix_decomp;
  MatrixRowDecompose(simple_hint, matrix_decomp, fparm.get_b_decomp(),
                     fparm.get_z_decomp(), fparm.get_t_decomp());
  MatrixTranspose(matrix_decomp, double_server_hint);

  const uint64_t* matrix_d2_flat = fparm.get_persudo_matrix_doublepir_flat();
  MatrixMultiplicationFlat(double_client_hint, double_server_hint,
                           matrix_d2_flat, poly_degree, rlwe_cmod);

  std::vector<std::vector<std::vector<uint64_t>>> ksk_a(
      expo,
      std::vector<std::vector<uint64_t>>(t_auto,
                                         std::vector<uint64_t>(poly_degree)));
  prng.refresh(kThirdDimensionSeed);
  for (uint64_t i = 0; i < expo; ++i) {
    PseudorandomMatrixGenerate(ksk_a[i], rlwe_cmod, prng);
  }

  const uint64_t mod_inv = ModInverse(static_cast<int64_t>(pack_num),
                                      static_cast<int64_t>(rlwe_cmod));
  YpirHexlNtt& ntt = fparm.get_ntt();
  for (uint64_t i = 0; i < pack_num; ++i) {
    Cdks21Lwe2RlweInplace(double_client_hint[i].data(), poly_degree, rlwe_cmod,
                         ntt);
    EltwiseFMAMod(double_client_hint[i].data(), double_client_hint[i].data(),
                  mod_inv, nullptr, poly_degree, rlwe_cmod);
  }

  h0 = PackrlwePreprocess(double_client_hint, GetLog2(pack_num),
                          GetLog2(poly_degree) - GetLog2(pack_num), ksk_a,
                          decomp_buf, fparm);
}

void YpirAnswer(const uint8_t* db, uint32_t* qu0, std::vector<uint64_t>& qu1,
                const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
                std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
                const std::vector<std::vector<uint64_t>>& server_hint,
                std::vector<std::vector<uint64_t>>& res,
                const FheParams& fparm, const PirParams& pparm) {
  const uint64_t lwe_dimension = fparm.get_lwe_dimension();
  const uint64_t poly_degree = fparm.get_poly_degree();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();
  const uint64_t t_decomp = fparm.get_t_decomp();
  const uint64_t pack_num = lwe_dimension * t_decomp;
  const uint64_t mod_inv = ModInverse(static_cast<int64_t>(pack_num),
                                      static_cast<int64_t>(rlwe_cmod));
  const uint64_t pack_num_log2 = GetLog2(pack_num);
  const uint64_t t_decomp_log2 = GetLog2(t_decomp);
  const uint64_t poly_degree_log2 = GetLog2(poly_degree);

  res.reserve(res.size() + 3);

  std::vector<std::vector<uint64_t>> h3(t_decomp,
                                        std::vector<uint64_t>(poly_degree, 0));
  std::vector<uint64_t> h2(lwe_dimension * t_decomp, 0);
  std::vector<uint64_t> h4(t_decomp, 0);

  DoublepirAnswer(db, qu0, qu1, server_hint, h2, h3, h4, fparm, pparm);

  for (uint64_t i = 0; i < pack_num; ++i) {
    h2[i] =
        (static_cast<unsigned __int128>(h2[i]) * mod_inv) % rlwe_cmod;
  }

  uint64_t ptr = 0;
  res.push_back(PackrlweOnlineConstantRows(
      h2, pack_num_log2, poly_degree_log2 - pack_num_log2, ksk_b, decomp_buf,
      ptr, fparm));

  std::vector<std::vector<uint64_t>> h4_matrix(
      t_decomp, std::vector<uint64_t>(poly_degree, 0));
  const uint64_t mod_inv_2 = ModInverse(static_cast<int64_t>(t_decomp),
                                        static_cast<int64_t>(rlwe_cmod));
  YpirHexlNtt& ntt = fparm.get_ntt();

  for (uint64_t i = 0; i < t_decomp; ++i) {
    Cdks21Lwe2RlweInplace(h3[i].data(), poly_degree, rlwe_cmod, ntt);
    h4_matrix[i][0] = h4[i];
    ntt.Forward(h4_matrix[i].data(), poly_degree);
    EltwiseFMAMod(h3[i].data(), h3[i].data(), mod_inv_2, nullptr, poly_degree,
                  rlwe_cmod);
    EltwiseFMAMod(h4_matrix[i].data(), h4_matrix[i].data(), mod_inv_2, nullptr,
                  poly_degree, rlwe_cmod);
  }

  const auto& ksk_a = fparm.get_persudo_hcube_ypir();
  std::vector<std::vector<std::vector<uint64_t>>> decomp_b_buf;
  ptr = 0;
  res.push_back(PackrlwePreprocess(h3, t_decomp_log2,
                                   poly_degree_log2 - t_decomp_log2, ksk_a,
                                   decomp_b_buf, fparm));
  res.push_back(PackrlweOnline(h4_matrix, t_decomp_log2,
                               poly_degree_log2 - t_decomp_log2, ksk_b,
                               decomp_b_buf, ptr, fparm));
}

}  // namespace psi::ypir::ypir_internal
