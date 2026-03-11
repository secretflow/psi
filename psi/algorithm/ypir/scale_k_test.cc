#include "gtest/gtest.h"

#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/params.h"
#include "psi/algorithm/ypir/server.h"

namespace psi::ypir {

TEST(ScaleKTest, VerifyScaleK) {
  // SimplePIR parameters
  size_t poly_len = 2048;
  std::vector<uint64_t> moduli{268369921, 249561089};
  double noise_width = 16.042421;
  PolyMatrixParams poly_matrix_params(2, 16384, 21, 4, 8, 8, 1);
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));

  uint64_t modulus = params.Modulus();
  uint64_t pt_modulus = params.PtModulus();
  uint64_t scale_k = params.ScaleK();

  // Verify scale_k ≈ modulus / pt_modulus
  uint64_t expected_scale_k = modulus / pt_modulus;
  EXPECT_EQ(scale_k, expected_scale_k);

  // Test rescale
  uint64_t test_value = 100;
  uint64_t scaled = scale_k * test_value;

  // Rescale back
  uint64_t rescaled = arith::Rescale(scaled, modulus, pt_modulus);

  EXPECT_EQ(rescaled, test_value);
}

}  // namespace psi::ypir
