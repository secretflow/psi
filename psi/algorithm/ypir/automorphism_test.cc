#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"

namespace psi::ypir {

using namespace psi::spiral;

TEST(AutomorphismTest, ZeroInputShouldGiveZeroOutput) {
  spdlog::set_level(spdlog::level::info);

  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 6.4;
  PolyMatrixParams poly_matrix_params;
  poly_matrix_params.n_ = 2;
  poly_matrix_params.pt_modulus_ = 16384;
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width, poly_matrix_params,
                query_params);

  SPDLOG_INFO("=== Testing AutomorphismPolyUncrtd with Zero Input ===");

  size_t dual_crt_size = params.CrtCount() * poly_len;
  std::vector<uint64_t> zero_input(dual_crt_size, 0);
  std::vector<uint64_t> output(dual_crt_size, 999);

  std::vector<size_t> t_values = {65, 129, 257, 513, 1025, 2049};

  for (size_t t : t_values) {
    std::fill(output.begin(), output.end(), 999);

    AutomorphismPolyUncrtd(params, absl::MakeSpan(output),
                           absl::MakeConstSpan(zero_input), t);

    bool all_zero = true;
    for (size_t i = 0; i < output.size(); ++i) {
      if (output[i] != 0) {
        all_zero = false;
        if (i < 10) {
          SPDLOG_ERROR("t={}: output[{}] = {} (expected 0)", t, i, output[i]);
        }
      }
    }

    if (all_zero) {
      SPDLOG_INFO("✓ t={}: All outputs are zero", t);
    } else {
      SPDLOG_ERROR("✗ t={}: Found non-zero outputs!", t);
    }

    EXPECT_TRUE(all_zero) << "Zero input should give zero output for t=" << t;
  }
}

TEST(AutomorphismTest, NonZeroInputCheck) {
  spdlog::set_level(spdlog::level::info);

  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 6.4;
  PolyMatrixParams poly_matrix_params;
  poly_matrix_params.n_ = 2;
  poly_matrix_params.pt_modulus_ = 16384;
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width, poly_matrix_params,
                query_params);

  SPDLOG_INFO(
      "=== Testing AutomorphismPolyUncrtd with Simple Non-Zero Input ===");

  size_t dual_crt_size = params.CrtCount() * poly_len;
  std::vector<uint64_t> input(dual_crt_size, 0);
  std::vector<uint64_t> output(dual_crt_size, 0);

  input[0] = 1;

  size_t t = 65;
  AutomorphismPolyUncrtd(params, absl::MakeSpan(output),
                         absl::MakeConstSpan(input), t);

  SPDLOG_INFO("Input: first coefficient = 1, others = 0");
  SPDLOG_INFO("t = {}", t);
  SPDLOG_INFO("First 10 output values:");
  for (size_t i = 0; i < 10; ++i) {
    SPDLOG_INFO("  output[{}] = {}", i, output[i]);
  }

  EXPECT_EQ(output[0], 1) << "output[0] should be 1";
}

}  // namespace psi::ypir
