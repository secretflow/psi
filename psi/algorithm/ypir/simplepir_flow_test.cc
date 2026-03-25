#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/server.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

uint64_t BytesToU64(const std::vector<uint8_t>& bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < bytes.size(); ++i) {
    value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
  }
  return value;
}

psi::pir::RawDatabase BuildSimplepirDatabase(const YpirParameters& params) {
  std::vector<std::vector<uint8_t>> rows(params.db_rows,
                                         std::vector<uint8_t>(params.db_cols * sizeof(uint16_t), 0));
  for (uint64_t row = 0; row < params.db_rows; ++row) {
    for (uint64_t col = 0; col < params.db_cols; ++col) {
      const uint16_t value = static_cast<uint16_t>((row * 17 + col * 3) % params.spiral_params.PtModulus());
      std::memcpy(rows[row].data() + col * sizeof(uint16_t), &value, sizeof(value));
    }
  }
  return psi::pir::RawDatabase(std::move(rows));
}

TEST(YpirSimplepirFlowTest, EndToEndSmallDatabase) {
  auto params = CreateSmallTestParamsSimplePIR();
  YpirClient client(params);
  YpirServer<uint16_t> server(params);
  server.GenerateFromRawData(BuildSimplepirDatabase(params));

  const uint64_t row = 123;
  const uint64_t col = 456;
  const uint64_t raw_idx = row * params.db_cols + col;

  const auto query_buffer = client.GenerateQueryBuffer(raw_idx);
  const auto response_buffer = server.Response(query_buffer);
  const auto decoded = client.DecodeResponseBuffer(response_buffer, raw_idx);

  const uint64_t expected = (row * 17 + col * 3) % params.spiral_params.PtModulus();
  EXPECT_EQ(BytesToU64(decoded), expected);
}

}  // namespace
}  // namespace psi::ypir
