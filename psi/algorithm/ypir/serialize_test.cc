#include "psi/algorithm/ypir/serialize.h"

#include <vector>

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

TEST(YpirSerializeTest, QueryRoundTrip) {
  YpirQuery query;
  query.mode = YpirMode::kDoublepir;
  query.packed_query_row = {1, 2, 3};
  query.qu0 = {4, 5};
  query.qu1 = {6, 7, 8};
  query.ksk_b = {{{9, 10}, {11}}, {{12, 13, 14}}};

  const auto buffer = SerializeQuery(query);
  const auto decoded = DeserializeQuery(buffer);

  EXPECT_EQ(decoded.mode, query.mode);
  EXPECT_EQ(decoded.packed_query_row, query.packed_query_row);
  EXPECT_EQ(decoded.qu0, query.qu0);
  EXPECT_EQ(decoded.qu1, query.qu1);
  EXPECT_EQ(decoded.ksk_b, query.ksk_b);
}

TEST(YpirSerializeTest, ResponseRoundTrip) {
  YpirResponse response;
  response.mode = YpirMode::kDoublepir;
  response.simplepir_response = {15, 16, 17};
  response.doublepir_response = {{18, 19}, {20}};

  const auto buffer = SerializeResponse(response);
  const auto decoded = DeserializeResponse(buffer);

  EXPECT_EQ(decoded.mode, response.mode);
  EXPECT_EQ(decoded.simplepir_response, response.simplepir_response);
  EXPECT_EQ(decoded.doublepir_response, response.doublepir_response);
}

}  // namespace
}  // namespace psi::ypir
