#include "psi/algorithm/ypir/client.h"

#include <cstdint>

#include "gtest/gtest.h"

#include "psi/algorithm/ypir/serialize.h"

namespace psi::ypir {
namespace {

TEST(YpirClientTest, SimplepirQueryUsesPackedRowShape) {
  auto params = CreateSmallTestParamsSimplePIR();
  YpirClient client(params);

  const auto query = client.GenerateQuery(123ULL * params.db_cols + 456ULL);
  EXPECT_EQ(query.mode, YpirMode::kSimplepir);
  EXPECT_EQ(query.packed_query_row.size(), params.db_rows);
  EXPECT_TRUE(query.qu0.empty());
  EXPECT_TRUE(query.qu1.empty());
  EXPECT_TRUE(query.ksk_b.empty());

  const auto buffer = client.GenerateQueryBuffer(7);
  const auto decoded = DeserializeQuery(buffer);
  EXPECT_EQ(decoded.mode, YpirMode::kSimplepir);
  EXPECT_EQ(decoded.packed_query_row.size(), params.db_rows);
}

TEST(YpirClientTest, DoublepirQueryContainsLegacyInternalPayload) {
  auto params = CreateSmallTestParamsDoublePIR();
  YpirClient client(params);

  const auto query = client.GenerateQuery(111ULL * params.db_cols + 222ULL);
  EXPECT_EQ(query.mode, YpirMode::kDoublepir);
  EXPECT_TRUE(query.packed_query_row.empty());
  EXPECT_FALSE(query.qu0.empty());
  EXPECT_FALSE(query.qu1.empty());
  EXPECT_FALSE(query.ksk_b.empty());

  const auto buffer = client.GenerateQueryBuffer(3);
  const auto decoded = DeserializeQuery(buffer);
  EXPECT_EQ(decoded.mode, YpirMode::kDoublepir);
  EXPECT_FALSE(decoded.qu0.empty());
  EXPECT_FALSE(decoded.qu1.empty());
  EXPECT_FALSE(decoded.ksk_b.empty());
}

}  // namespace
}  // namespace psi::ypir
