// Test FastBatchedDotProduct to verify correctness
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/ypir/util.h"

namespace psi::ypir {

using namespace psi::spiral;

TEST(FastDotProductTest, AllZerosInput) {
  // Test with all-zeros database
  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 6.4;

  PolyMatrixParams poly_matrix_params(2, 256, 21, 4, 8, 8, 1);
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));

  size_t db_rows = 1ULL << (params.DbDim1() + params.PolyLenLog2());
  size_t db_cols = params.Instances() * params.PolyLen();

  // Create all-zeros database with uint8_t
  std::vector<uint8_t> db(db_rows * db_cols, 0);

  // Create query vector (all zeros except position 0)
  std::vector<uint64_t> query(db_rows, 0);
  query[0] = 1;

  // Result vector
  std::vector<uint64_t> result(db_cols, 0);

  SPDLOG_INFO("Testing FastBatchedDotProduct with all-zeros database");
  SPDLOG_INFO("db_rows={}, db_cols={}", db_rows, db_cols);

  // Call FastBatchedDotProduct
  // Signature: (params, c, a, a_elems, b_t, b_rows, b_cols)
  FastBatchedDotProduct<uint8_t>(params, result.data(), query.data(), db_rows,
                                 db.data(), db_rows, db_cols);

  SPDLOG_INFO("Result size: {}", result.size());

  // Check result - should be all zeros since database is all zeros
  bool all_zero = true;
  for (size_t i = 0; i < std::min(size_t(10), result.size()); ++i) {
    SPDLOG_INFO("  result[{}] = {}", i, result[i]);
    if (result[i] != 0) {
      all_zero = false;
    }
  }

  EXPECT_TRUE(all_zero)
      << "FastBatchedDotProduct with all-zeros should return all zeros";
}

TEST(FastDotProductTest, SimplePattern) {
  // Test with pattern database
  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 6.4;

  PolyMatrixParams poly_matrix_params(2, 256, 21, 4, 8, 8, 1);
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));

  size_t db_rows = 1ULL << (params.DbDim1() + params.PolyLenLog2());
  size_t db_cols = params.Instances() * params.PolyLen();

  // Create database with pattern: db[i] = i % 256
  std::vector<uint8_t> db(db_rows * db_cols);
  for (size_t col = 0; col < db_cols; ++col) {
    for (size_t row = 0; row < db_rows; ++row) {
      // Column-major storage: db[col * db_rows + row]
      db[col * db_rows + row] =
          static_cast<uint8_t>((col * db_rows + row) % 256);
    }
  }

  // Create query selecting first row
  std::vector<uint64_t> query(db_rows, 0);
  query[0] = 1;  // Select first row

  // Result vector
  std::vector<uint64_t> result(db_cols, 0);

  SPDLOG_INFO("Testing FastBatchedDotProduct with pattern database");
  SPDLOG_INFO("db_rows={}, db_cols={}", db_rows, db_cols);

  FastBatchedDotProduct<uint8_t>(params, result.data(), query.data(), db_rows,
                                 db.data(), db_rows, db_cols);

  SPDLOG_INFO("Result data (first 10):");
  for (size_t i = 0; i < std::min(size_t(10), result.size()); ++i) {
    // Expected value is the first element of column i (row 0)
    uint64_t expected = db[i * db_rows + 0];
    SPDLOG_INFO("  result[{}] = {} (expected {})", i, result[i], expected);
  }
}

}  // namespace psi::ypir
