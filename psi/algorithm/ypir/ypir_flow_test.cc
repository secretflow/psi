#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/serialize.h"
#include "psi/algorithm/ypir/server.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

psi::pir::RawDatabase BuildDoublepirDatabase(const YpirParameters& params) {
  std::vector<std::vector<uint8_t>> rows(params.db_rows,
                                         std::vector<uint8_t>(params.db_cols, 0));
  for (uint64_t row = 0; row < params.db_rows; ++row) {
    for (uint64_t col = 0; col < params.db_cols; ++col) {
      rows[row][col] = static_cast<uint8_t>((row + col) % 251);
    }
  }
  return psi::pir::RawDatabase(std::move(rows));
}

TEST(YpirDoublepirFlowTest, EndToEndSmallDatabase) {
  auto params = CreateSmallTestParamsDoublePIR();
  YpirClient client(params);
  YpirServer<uint8_t> server(params);
  server.GenerateFromRawData(BuildDoublepirDatabase(params));

  const auto state = server.PerformOfflinePrecomputation();
  const uint64_t row = 111;
  const uint64_t col = 222;
  const uint64_t raw_idx = row * params.db_cols + col;

  const auto query_buffer = client.GenerateQueryBuffer(raw_idx);
  const auto response = server.ProcessQuery(DeserializeQuery(query_buffer), state);
  const auto decoded = client.DecodeResponse(response, raw_idx);

  ASSERT_EQ(decoded.size(), 1U);
  EXPECT_EQ(decoded[0], static_cast<uint8_t>((row + col) % 251));
}

}  // namespace
}  // namespace psi::ypir
