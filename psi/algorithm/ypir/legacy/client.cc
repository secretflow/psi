#include "psi/algorithm/ypir/legacy/client.h"

#include <chrono>
#include <cmath>
#include <random>
#include <vector>

namespace psi::ypir::ypir_internal {
namespace {

uint64_t MakeSeedMaterial() {
  const uint64_t time_seed = static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::random_device rd;
  return time_seed ^ (static_cast<uint64_t>(rd()) << 32) ^ rd();
}

std::mt19937_64& GlobalPrng() {
  thread_local std::mt19937_64 rng(MakeSeedMaterial());
  return rng;
}

uint64_t GetBase(uint64_t b, uint64_t z, uint64_t ti) {
  return 1ULL << (b + ti * z);
}

void LweEncrypt(Secret& sk, const std::vector<uint64_t>& a, uint64_t message,
                uint64_t& b, uint64_t pmod, double sig) {
  const uint64_t lwe_dimension = sk.get_len();
  const uint64_t cmod = sk.get_mod();
  const long double delta =
      static_cast<long double>(cmod) / static_cast<long double>(pmod);

  const uint64_t e = SampleGauss(sig, cmod, GlobalPrng());

  std::vector<uint64_t> tmp(lwe_dimension, 0);
  EltwiseMultMod(tmp.data(), a.data(), sk.data.data(), lwe_dimension, cmod);
  b = 0;
  for (uint64_t i = 0; i < lwe_dimension; ++i) {
    b = (b + tmp[i]) % cmod;
  }
  b = (b + e) % cmod;
  b = (b + static_cast<uint64_t>(message * delta)) % cmod;
}

void LweDecrypt(Secret& sk, std::vector<uint64_t>& a, uint64_t& message,
                uint64_t& b, uint64_t pmod) {
  const uint64_t lwe_dimension = sk.get_len();
  const uint64_t cmod = sk.get_mod();
  const long double delta =
      static_cast<long double>(cmod) / static_cast<long double>(pmod);

  std::vector<uint64_t> tmp(lwe_dimension, 0);
  EltwiseMultMod(tmp.data(), a.data(), sk.data.data(), lwe_dimension, cmod);
  for (uint64_t i = 0; i < lwe_dimension; ++i) {
    b = (cmod + b - tmp[i]) % cmod;
  }

  if (b > (cmod >> 1)) {
    b = static_cast<uint64_t>(std::llround(
            (static_cast<long double>(b) - static_cast<long double>(cmod)) /
            delta)) %
        pmod;
  } else {
    b = static_cast<uint64_t>(
            std::llround(static_cast<long double>(b) / delta)) %
        pmod;
  }
  message = b;
}

void PowerOfBase(const uint64_t* vec, uint64_t& num, uint64_t b, uint64_t z,
                 uint64_t t) {
  num = 0;
  for (uint64_t i = 0; i < t; ++i) {
    num = (num << z) | vec[t - 1 - i];
  }
  num <<= b;
}

void RlweEncode(Secret& sk, const std::vector<uint64_t>& a,
                std::vector<uint64_t>& message, std::vector<uint64_t>& b,
                const FheParams& fparm) {
  const uint64_t poly_degree = fparm.get_poly_degree();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();
  const double sig = fparm.get_sig_ring();

  YpirHexlNtt& ntt = fparm.get_ntt();
  if (!sk.get_ntt_form()) {
    ntt.Forward(sk.data.data(), poly_degree);
    sk.switch_ntt_format();
  }

  std::vector<uint64_t> err(poly_degree, 0);
  SampleGauss(err, sig, rlwe_cmod, GlobalPrng());
  EltwiseFMAMod(err.data(), message.data(), 1, err.data(), poly_degree,
                rlwe_cmod);
  ntt.Forward(err.data(), poly_degree);

  EltwiseMultMod(b.data(), sk.data.data(), a.data(), poly_degree, rlwe_cmod);
  EltwiseAddMod(b.data(), err.data(), b.data(), poly_degree, rlwe_cmod);
}

void RlweDecrypt(Secret& sk, std::vector<uint64_t>& a,
                 std::vector<uint64_t>& message, std::vector<uint64_t>& b,
                 const FheParams& fparm) {
  const uint64_t poly_degree = fparm.get_poly_degree();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();
  const uint64_t rlwe_pmod = fparm.get_rlwe_pmod();
  const uint64_t delta = rlwe_cmod / rlwe_pmod;

  YpirHexlNtt& ntt = fparm.get_ntt();
  if (!sk.get_ntt_form()) {
    ntt.Forward(sk.data.data(), poly_degree);
    sk.switch_ntt_format();
  }

  std::vector<uint64_t> tmp(poly_degree, 0);
  EltwiseMultMod(tmp.data(), sk.data.data(), a.data(), poly_degree, rlwe_cmod);
  EltwiseSubMod(b.data(), b.data(), tmp.data(), poly_degree, rlwe_cmod);
  ntt.Inverse(b.data(), poly_degree);

  message.assign(poly_degree, 0);
  for (uint64_t i = 0; i < poly_degree; ++i) {
    if (b[i] > (rlwe_cmod >> 1)) {
      message[i] = static_cast<uint64_t>(
                       std::llround((static_cast<long double>(b[i]) -
                                     static_cast<long double>(rlwe_cmod)) /
                                    static_cast<long double>(delta))) %
                   rlwe_pmod;
    } else {
      message[i] =
          static_cast<uint64_t>(std::llround(static_cast<long double>(b[i]) /
                                             static_cast<long double>(delta))) %
          rlwe_pmod;
    }
  }
}

void MatrixReconstruct(const std::vector<uint64_t>& vec,
                       std::vector<std::vector<uint64_t>>& mat, uint64_t row,
                       uint64_t col) {
  mat.assign(row, std::vector<uint64_t>(col, 0));
  uint64_t ptr = 0;
  for (uint64_t i = 0; i < row; ++i) {
    for (uint64_t j = 0; j < col; ++j, ++ptr) {
      mat[i][j] = vec[ptr];
    }
  }
}

void GadgetEncrypt(Secret& sk, const std::vector<std::vector<uint64_t>>& a,
                   std::vector<uint64_t>& message,
                   std::vector<std::vector<uint64_t>>& b,
                   const FheParams& fparm) {
  const uint64_t b_auto = fparm.get_b_auto();
  const uint64_t z_auto = fparm.get_z_auto();
  const uint64_t t_auto = fparm.get_t_auto();
  const uint64_t poly_degree = fparm.get_poly_degree();
  const uint64_t rlwe_cmod = fparm.get_rlwe_cmod();

  std::vector<uint64_t> message_tmp(poly_degree, 0);
  for (uint64_t i = 0; i < t_auto; ++i) {
    const uint64_t base = GetBase(b_auto, z_auto, i) % rlwe_cmod;
    EltwiseFMAMod(message_tmp.data(), message.data(), base, nullptr,
                  poly_degree, rlwe_cmod);
    RlweEncode(sk, a[i], message_tmp, b[i], fparm);
  }
}

void GenerateAutokey(
    Secret& sk, const std::vector<std::vector<std::vector<uint64_t>>>& ksk_a,
    std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    const FheParams& fparm) {
  const uint64_t degree = fparm.get_poly_degree();
  const uint64_t cmod = fparm.get_rlwe_cmod();
  const uint64_t expo = GetLog2(degree);

  YpirHexlNtt& ntt = fparm.get_ntt();
  if (sk.get_ntt_form()) {
    ntt.Inverse(sk.data.data(), degree);
    sk.switch_ntt_format();
  }

  std::vector<uint64_t> newkey(degree, 0);
  const std::vector<uint64_t> copykey = sk.data;
  for (uint64_t i = 0; i < expo; ++i) {
    const uint64_t idx = (1ULL << (expo - i)) + 1;
    ApplyAutoCoefForm(newkey, copykey, static_cast<int32_t>(idx), cmod);
    GadgetEncrypt(sk, ksk_a[i], newkey, ksk_b[i], fparm);
  }
}

}  // namespace

void YpirRecover(Secret& simple_sk, Secret& double_sk,
                 std::vector<std::vector<uint64_t>>& res, uint64_t& message,
                 const FheParams& fparm, const PirParams&) {
  const uint64_t degree = fparm.get_poly_degree();
  const uint64_t lwe_pmod = fparm.get_lwe_pmod();
  const uint64_t lwe_dimension = fparm.get_lwe_dimension();
  const uint64_t b = fparm.get_b_decomp();
  const uint64_t z = fparm.get_z_decomp();
  const uint64_t t = fparm.get_t_decomp();

  std::vector<uint64_t> pack_a_message(degree, 0);
  std::vector<uint64_t> pack_b_message(degree, 0);
  RlweDecrypt(double_sk, res[0], pack_a_message, res[1], fparm);
  RlweDecrypt(double_sk, res[2], pack_b_message, res[3], fparm);

  std::vector<std::vector<uint64_t>> matrix_pack_a_message;
  MatrixReconstruct(pack_a_message, matrix_pack_a_message, lwe_dimension, t);

  std::vector<uint64_t> a(lwe_dimension, 0);
  for (uint64_t i = 0; i < lwe_dimension; ++i) {
    PowerOfBase(matrix_pack_a_message[i].data(), a[i], b, z, t);
  }

  uint64_t b_value = 0;
  PowerOfBase(pack_b_message.data(), b_value, b, z, t);
  LweDecrypt(simple_sk, a, message, b_value, lwe_pmod);
}

YpirQuery Generate_query_ypir(uint64_t c_idx, uint64_t r_idx, Secret& lwe_sk,
                              Secret& rlwe_sk, AESCTR_PRNG& prng,
                              const FheParams& fparm, const PirParams& pparm) {
  YpirQuery out;

  const uint64_t cols = pparm.get_col();
  std::vector<uint64_t> query_vec_col(cols, 0);
  query_vec_col[c_idx] = 1;

  out.qu0.resize(cols);
  const double sig = fparm.get_sig();
  const uint64_t lwe_pmod = fparm.get_lwe_pmod();
  const auto& matrix0 = fparm.get_persudo_matrix_simplepir();
  for (uint64_t i = 0; i < cols; ++i) {
    LweEncrypt(lwe_sk, matrix0[i], query_vec_col[i], out.qu0[i], lwe_pmod, sig);
  }

  const uint64_t rows = pparm.get_row();
  const uint64_t degree = fparm.get_poly_degree();
  std::vector<uint64_t> query_vec_row(rows, 0);
  query_vec_row[r_idx] = 1;

  out.qu1.resize(rows);
  const double sig_ring = fparm.get_sig_ring();
  const uint64_t rlwe_pmod = fparm.get_rlwe_pmod();
  std::vector<std::vector<uint64_t>> matrix1(rows,
                                             std::vector<uint64_t>(degree, 0));
  prng.refresh(kSecondDimensionSeed);
  PseudorandomMatrixGenerate(matrix1, fparm.get_rlwe_cmod(), prng);
  for (uint64_t i = 0; i < rows; ++i) {
    LweEncrypt(rlwe_sk, matrix1[i], query_vec_row[i], out.qu1[i], rlwe_pmod,
               sig_ring);
  }

  const uint64_t t_auto = fparm.get_t_auto();
  const uint64_t expo = GetLog2(degree);
  out.ksk_b.assign(expo, std::vector<std::vector<uint64_t>>(
                             t_auto, std::vector<uint64_t>(degree, 0)));

  std::vector<std::vector<std::vector<uint64_t>>> ksk_a(
      expo, std::vector<std::vector<uint64_t>>(
                t_auto, std::vector<uint64_t>(degree, 0)));
  prng.refresh(kThirdDimensionSeed);
  for (uint64_t i = 0; i < expo; ++i) {
    PseudorandomMatrixGenerate(ksk_a[i], fparm.get_rlwe_cmod(), prng);
  }
  GenerateAutokey(rlwe_sk, ksk_a, out.ksk_b, fparm);

  return out;
}

}  // namespace psi::ypir::ypir_internal
