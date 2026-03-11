#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "gtest/gtest.h"

#include "psi/algorithm/ypir/byhe_util.h"

namespace psi::ypir::byhe {
namespace {

std::vector<uint32_t> ReferenceMatVecU8U32Mod2p32(const std::vector<uint8_t>& A,
                                                  const std::vector<uint32_t>& x,
                                                  size_t rows, size_t cols) {
  std::vector<uint32_t> y(rows, 0);
  for (size_t r = 0; r < rows; ++r) {
    uint64_t acc = 0;
    for (size_t c = 0; c < cols; ++c) {
      acc += static_cast<uint64_t>(A[r * cols + c]) * x[c];
    }
    y[r] = static_cast<uint32_t>(acc);
  }
  return y;
}

void RunMatVecCase(size_t rows, size_t cols) {
  std::mt19937 rng(static_cast<uint32_t>(rows * 131 + cols * 17 + 7));
  std::uniform_int_distribution<int> a_dist(0, 255);
  std::uniform_int_distribution<uint32_t> x_dist(
      0, std::numeric_limits<uint32_t>::max());

  std::vector<uint8_t> A(rows * cols, 0);
  std::vector<uint32_t> x(cols, 0);
  for (auto& v : A) {
    v = static_cast<uint8_t>(a_dist(rng));
  }
  for (auto& v : x) {
    v = x_dist(rng);
  }

  std::vector<uint32_t> y(rows, 0);
  MatVecU8U32Mod2p32(A.data(), x.data(), y.data(), rows, cols);

  const auto expected = ReferenceMatVecU8U32Mod2p32(A, x, rows, cols);
  EXPECT_EQ(y, expected);
}

}  // namespace

TEST(ByheUtilTest, MatVecU8U32Mod2p32MatchesReference) {
  RunMatVecCase(1, 1);
  RunMatVecCase(7, 13);
  RunMatVecCase(16, 64);
  RunMatVecCase(31, 255);
  RunMatVecCase(64, 512);
}

}  // namespace psi::ypir::byhe
