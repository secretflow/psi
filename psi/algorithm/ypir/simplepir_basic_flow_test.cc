// Basic flow test - test individual components
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/ypir/util.h"

namespace psi::ypir {

using namespace psi::spiral;

TEST(SimplePIRBasicFlowTest, TestQueryDatabaseMatrixMultiplication) {
  // Test the basic matrix multiplication: query * database
  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 16.042421;

  PolyMatrixParams poly_matrix_params(2, 16384, 21, 4, 8, 8, 1);
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));

  size_t db_rows = 1ULL << (params.DbDim1() + params.PolyLenLog2());
  size_t db_cols = params.Instances() * params.PolyLen();

  SPDLOG_INFO("=== Basic Flow Test ===");
  SPDLOG_INFO("db_rows={}, db_cols={}", db_rows, db_cols);

  // Create all-zeros database
  std::vector<uint16_t> db(db_rows * db_cols, 0);

  // Create simple query - select first row
  std::vector<uint64_t> query(db_rows, 0);
  query[0] = params.ScaleK();  // scale_k at position 0

  SPDLOG_INFO("Query[0] = {} (scale_k)", query[0]);

  // Pack query for FastBatchedDotProduct
  std::vector<uint64_t> query_packed(db_rows);
  const uint64_t m0 = params.Moduli(0);
  const uint64_t m1 = params.Moduli(1);

  for (size_t i = 0; i < db_rows; ++i) {
    query_packed[i] = (query[i] % m0) | ((query[i] % m1) << 32);
  }

  SPDLOG_INFO("Packed query[0] = 0x{:016x}", query_packed[0]);

  // Result
  std::vector<uint64_t> result(db_cols, 0);

  // Call FastBatchedDotProduct
  FastBatchedDotProduct<uint16_t>(params, result.data(), query_packed.data(),
                                  db_rows, db.data(), db_rows, db_cols);

  SPDLOG_INFO("Result after dot product (first 10):");
  bool all_zero = true;
  for (size_t i = 0; i < std::min(size_t(10), result.size()); ++i) {
    SPDLOG_INFO("  result[{}] = {}", i, result[i]);
    if (result[i] != 0) {
      all_zero = false;
    }
  }

  if (all_zero) {
    SPDLOG_INFO("✓ All results are zero as expected");
  } else {
    SPDLOG_ERROR("✗ Got non-zero results for zero database!");
  }

  EXPECT_TRUE(all_zero) << "Result should be all zeros for zero database";
}

TEST(SimplePIRBasicFlowTest, TestQueryDatabaseWithData) {
  // Test with non-zero database
  size_t poly_len = 2048;
  std::vector<uint64_t> moduli = {268369921, 249561089};
  double noise_width = 16.042421;

  PolyMatrixParams poly_matrix_params(2, 16384, 21, 4, 8, 8, 1);
  QueryParams query_params(1, 1, 1);

  Params params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));

  size_t db_rows = 1ULL << (params.DbDim1() + params.PolyLenLog2());
  size_t db_cols = params.Instances() * params.PolyLen();

  SPDLOG_INFO("=== Basic Flow Test with Data ===");

  // Create database with pattern
  std::vector<uint16_t> db(db_rows * db_cols);
  for (size_t col = 0; col < db_cols; ++col) {
    for (size_t row = 0; row < db_rows; ++row) {
      // Column-major: db[col * db_rows + row]
      db[col * db_rows + row] = static_cast<uint16_t>((row + col) % 100);
    }
  }

  SPDLOG_INFO("DB[0,0] = {}, DB[0,1] = {}, DB[1,0] = {}", db[0], db[1],
              db[db_rows]);

  // Query for row 0
  std::vector<uint64_t> query(db_rows, 0);
  query[0] = params.ScaleK();

  // Pack query
  std::vector<uint64_t> query_packed(db_rows);
  const uint64_t m0 = params.Moduli(0);
  const uint64_t m1 = params.Moduli(1);

  for (size_t i = 0; i < db_rows; ++i) {
    query_packed[i] = (query[i] % m0) | ((query[i] % m1) << 32);
  }

  // Result
  std::vector<uint64_t> result(db_cols, 0);

  FastBatchedDotProduct<uint16_t>(params, result.data(), query_packed.data(),
                                  db_rows, db.data(), db_rows, db_cols);

  SPDLOG_INFO("Result after dot product (first 10):");
  for (size_t i = 0; i < std::min(size_t(10), result.size()); ++i) {
    // Rescale result back to plaintext space
    uint64_t rescaled =
        arith::Rescale(result[i], params.Modulus(), params.PtModulus());
    uint16_t expected = db[i * db_rows + 0];  // First row, column i
    SPDLOG_INFO("  result[{}] = {} (rescaled: {}), expected: {}", i, result[i],
                rescaled, expected);
  }
  std::cout << "test" << std::endl;
}

}  // namespace psi::ypir
