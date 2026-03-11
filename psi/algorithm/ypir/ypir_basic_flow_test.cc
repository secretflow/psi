#include "gtest/gtest.h"

#include <chrono>
#include <iomanip>
#include <iostream>

#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/hexl.h"
#include "psi/algorithm/ypir/ypir_params.h"
#include "psi/algorithm/ypir/server.h"

namespace psi::ypir {

namespace {

static psi::ypir::byhe::ByheHexlNtt* g_ntt = nullptr;

void NttForwardWrapper(uint64_t* data, size_t len) {
  g_ntt->Forward(data, len);
}

template <typename Fn>
double MeasureElapsedMs(Fn&& fn) {
  const auto start = std::chrono::steady_clock::now();
  fn();
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace

TEST(YPIRBasicFlowTest, ByheYpirFlowSmoke) {
  using namespace psi::ypir::byhe;
  srand(1235);
  std::cout << std::fixed << std::setprecision(3);

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

  FheParams fparm(rlwe_degree, rlwe_cmod, rlwe_pmod, lwe_dimension, lwe_cmod,
                  lwe_pmod, sigma, sigma_ring, auto_params, decomp_params);

  const uint64_t db_row = 1 << 12;
  const uint64_t db_col = 1 << 12;
  PirParams pparm(db_row, db_col);

  g_ntt = &fparm.get_ntt();
  fparm.SetNttForward(&NttForwardWrapper);

  fparm.set_persudo_matrix_simplepir(pparm.get_col());
  fparm.set_persudo_matrix_doublepir(pparm.get_row());
  fparm.set_persudo_hcube_ypir();

  const uint64_t expo = GetLog2(rlwe_degree);
  std::vector<uint64_t> auto_idx;
  for (uint64_t i = 1; i <= expo; ++i) {
    auto_idx.push_back((1ULL << i) + 1);
  }
  fparm.set_automap(auto_idx);
  fparm.set_precomputed_pt(expo);

  // Build a small database and flatten to bytes.
  std::vector<std::vector<uint64_t>> db(
      pparm.get_row(), std::vector<uint64_t>(pparm.get_col(), 0));
  for (uint64_t i = 0; i < pparm.get_row(); ++i) {
    for (uint64_t j = 0; j < pparm.get_col(); ++j) {
      db[i][j] = static_cast<uint64_t>((i + j) % lwe_pmod);
    }
  }

  std::vector<uint8_t> db8(pparm.get_row() * pparm.get_col(), 0);
  for (uint64_t i = 0; i < pparm.get_row(); ++i) {
    for (uint64_t j = 0; j < pparm.get_col(); ++j) {
      db8[i * pparm.get_col() + j] = static_cast<uint8_t>(db[i][j]);
    }
  }

  std::vector<uint64_t> H0(rlwe_degree, 0);
  std::vector<std::vector<uint64_t>> double_server_hint;
  std::vector<std::vector<std::vector<uint64_t>>> decomp_buf;
  AESCTR_PRNG prng;
  const double hint_generate_ms = MeasureElapsedMs([&] {
    psi::ypir::byhe::YpirHintGenerate(db, H0, double_server_hint, decomp_buf,
                                      prng, fparm, pparm);
  });
  std::cout << "YpirHintGenerate took " << hint_generate_ms << " ms"
            << std::endl;

  std::vector<std::vector<uint64_t>> mat_tmp;
  MatrixTranspose(double_server_hint, mat_tmp);

  const uint64_t r_idx = rand() % db_row;
  const uint64_t c_idx = rand() % db_col;
  Secret simple_sk(lwe_dimension, lwe_cmod);
  Secret double_sk(rlwe_degree, rlwe_cmod);

  YpirQuery query;
  const double query_generate_ms = MeasureElapsedMs([&] {
    query = psi::ypir::byhe::Generate_query_ypir(c_idx, r_idx, simple_sk,
                                                 double_sk, prng, fparm,
                                                 pparm);
  });
  std::cout << "Generate_query_ypir took " << query_generate_ms << " ms"
            << std::endl;

  std::vector<uint32_t> qu0_32(query.qu0.size(), 0);
  for (size_t i = 0; i < query.qu0.size(); ++i) {
    qu0_32[i] = static_cast<uint32_t>(query.qu0[i]);
  }

  std::vector<std::vector<uint64_t>> res;
  res.push_back(H0);
  const double answer_ms = MeasureElapsedMs([&] {
    psi::ypir::byhe::YpirAnswer(db8.data(), qu0_32.data(), query.qu1,
                                query.ksk_b, decomp_buf, mat_tmp, res, fparm,
                                pparm);
  });
  std::cout << "YpirAnswer took " << answer_ms << " ms" << std::endl;

  uint64_t message = 0;
  const double recover_ms = MeasureElapsedMs([&] {
    psi::ypir::byhe::YpirRecover(simple_sk, double_sk, res, message, fparm,
                                 pparm);
  });
  std::cout << "YpirRecover took " << recover_ms << " ms" << std::endl;
  std::cout << message << " " << db[r_idx][c_idx] << std::endl;
  EXPECT_EQ(message, db[r_idx][c_idx]);
}

}  // namespace psi::ypir
