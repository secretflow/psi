#include "psi/algorithm/ypir/params.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "yacl/base/exception.h"

namespace psi::ypir {
namespace {

size_t Log2Exact(uint64_t v) {
  YACL_ENFORCE_GT(v, 0U);
  YACL_ENFORCE_EQ(v & (v - 1), 0U, "value must be a power of two");
  size_t out = 0;
  while (v > 1) {
    v >>= 1;
    ++out;
  }
  return out;
}

struct MatrixShape {
  uint64_t rows = 0;
  uint64_t cols = 0;
};

MatrixShape ChooseShape(uint64_t num_items, uint64_t base) {
  YACL_ENFORCE_GT(base, 0U);
  uint64_t rows = base;
  uint64_t cols = base;
  while (rows * cols < num_items) {
    if (rows <= cols) {
      rows <<= 1;
    } else {
      cols <<= 1;
    }
  }
  return {rows, cols};
}

YpirParameters BuildSimplepirParamsForShape(uint64_t db_rows, uint64_t db_cols,
                                            uint64_t item_size_bits) {
  const uint64_t poly_len = 1024;
  const uint64_t pt_modulus = item_size_bits <= 8 ? (1ULL << 8) : (1ULL << 14);
  YACL_ENFORCE_GE(db_rows, poly_len);
  YACL_ENFORCE_EQ(db_cols % poly_len, 0U,
                  "simplepir db_cols must be a multiple of {}", poly_len);
  const size_t db_dim_1 = Log2Exact(db_rows) - Log2Exact(poly_len);
  const size_t instances = db_cols / poly_len;

  std::vector<uint64_t> moduli = {268369921ULL, 249561089ULL};
  double noise_width = 16.042421;
  psi::spiral::PolyMatrixParams poly_matrix_params(2, pt_modulus, 21, 4, 8, 8,
                                                   instances);
  psi::spiral::QueryParams query_params(db_dim_1, 0, instances);
  psi::spiral::Params spiral_params(poly_len, std::move(moduli), noise_width,
                                    std::move(poly_matrix_params),
                                    std::move(query_params));

  YpirParameters out;
  out.mode = YpirMode::kSimplepir;
  out.spiral_params = std::move(spiral_params);
  out.db_rows =
      1ULL << (out.spiral_params.DbDim1() + out.spiral_params.PolyLenLog2());
  out.db_cols = out.spiral_params.Instances() * out.spiral_params.PolyLen();
  out.value_bytes = std::max<size_t>(1, (item_size_bits + 7) / 8);
  return out;
}

YpirParameters BuildDoublepirParamsForShape(uint64_t db_rows, uint64_t db_cols,
                                            uint64_t item_size_bits) {
  const uint64_t poly_len = 1024;
  YACL_ENFORCE_GE(db_rows, poly_len);
  YACL_ENFORCE_GE(db_cols, poly_len);
  const size_t db_dim_1 = Log2Exact(db_rows) - Log2Exact(poly_len);
  const size_t db_dim_2 = Log2Exact(db_cols) - Log2Exact(poly_len);

  std::vector<uint64_t> moduli = {268369921ULL, 249561089ULL};
  double noise_width = 6.4;
  psi::spiral::PolyMatrixParams poly_matrix_params(2, 1ULL << 15, 21, 4, 8, 8,
                                                   1);
  psi::spiral::QueryParams query_params(db_dim_1, db_dim_2, 1);
  psi::spiral::Params spiral_params(poly_len, std::move(moduli), noise_width,
                                    std::move(poly_matrix_params),
                                    std::move(query_params));

  YpirParameters out;
  out.mode = YpirMode::kDoublepir;
  out.spiral_params = std::move(spiral_params);
  out.db_rows =
      1ULL << (out.spiral_params.DbDim1() + out.spiral_params.PolyLenLog2());
  out.db_cols =
      1ULL << (out.spiral_params.DbDim2() + out.spiral_params.PolyLenLog2());
  out.value_bytes = std::max<size_t>(1, (item_size_bits + 7) / 8);
  return out;
}

}  // namespace

YpirParameters CreateParamsForScenarioSimplePIR(uint64_t num_items,
                                                uint64_t item_size_bits) {
  const auto shape = ChooseShape(num_items, 1024);
  return BuildSimplepirParamsForShape(shape.rows, shape.cols, item_size_bits);
}

YpirParameters CreateParamsForScenarioDoublePIR(uint64_t num_items,
                                                uint64_t item_size_bits) {
  const auto shape = ChooseShape(num_items, 1024);
  return BuildDoublepirParamsForShape(shape.rows, shape.cols, item_size_bits);
}

YpirParameters CreateParamsForShapeSimplePIR(uint64_t db_rows,
                                             uint64_t db_cols,
                                             uint64_t item_size_bits) {
  return BuildSimplepirParamsForShape(db_rows, db_cols, item_size_bits);
}

YpirParameters CreateParamsForShapeDoublePIR(uint64_t db_rows,
                                             uint64_t db_cols,
                                             uint64_t item_size_bits) {
  return BuildDoublepirParamsForShape(db_rows, db_cols, item_size_bits);
}

YpirParameters CreateSmallTestParamsSimplePIR() {
  return BuildSimplepirParamsForShape(1ULL << 10, 1ULL << 10, 16);
}

YpirParameters CreateSmallTestParamsDoublePIR() {
  return BuildDoublepirParamsForShape(1ULL << 10, 1ULL << 10, 8);
}

}  // namespace psi::ypir
