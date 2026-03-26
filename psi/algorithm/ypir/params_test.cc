#include "psi/algorithm/ypir/params.h"

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

TEST(YpirParamsTest, SmallSimplepirParamsMatchFastRegressionShape) {
  const auto params = CreateSmallTestParamsSimplePIR();
  EXPECT_EQ(params.mode, YpirMode::kSimplepir);
  EXPECT_EQ(params.db_rows, 1ULL << 10);
  EXPECT_EQ(params.db_cols, 1ULL << 10);
  EXPECT_EQ(params.value_bytes, 2U);
}

TEST(YpirParamsTest, SmallDoublepirParamsMatchFastRegressionShape) {
  const auto params = CreateSmallTestParamsDoublePIR();
  EXPECT_EQ(params.mode, YpirMode::kDoublepir);
  EXPECT_EQ(params.db_rows, 1ULL << 10);
  EXPECT_EQ(params.db_cols, 1ULL << 10);
  EXPECT_EQ(params.value_bytes, 1U);
}

TEST(YpirParamsTest, ScenarioHelpersRespectRequestedValueSize) {
  const auto simple_params =
      CreateParamsForScenarioSimplePIR((1ULL << 18) + 7, 8);
  const auto double_params =
      CreateParamsForScenarioDoublePIR((1ULL << 19) + 9, 24);

  EXPECT_GE(simple_params.NumItems(), (1ULL << 18) + 7);
  EXPECT_EQ(simple_params.value_bytes, 1U);
  EXPECT_GE(double_params.NumItems(), (1ULL << 19) + 9);
  EXPECT_EQ(double_params.value_bytes, 3U);
}

TEST(YpirParamsTest, ShapeHelpersPreserveRequestedShape) {
  const auto simple_params =
      CreateParamsForShapeSimplePIR(1ULL << 11, 3ULL << 10, 16);
  const auto double_params =
      CreateParamsForShapeDoublePIR(1ULL << 11, 1ULL << 12, 8);

  EXPECT_EQ(simple_params.db_rows, 1ULL << 11);
  EXPECT_EQ(simple_params.db_cols, 3ULL << 10);
  EXPECT_EQ(simple_params.value_bytes, 2U);
  EXPECT_EQ(double_params.db_rows, 1ULL << 11);
  EXPECT_EQ(double_params.db_cols, 1ULL << 12);
  EXPECT_EQ(double_params.value_bytes, 1U);
}

}  // namespace
}  // namespace psi::ypir
