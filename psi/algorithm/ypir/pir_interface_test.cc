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

template <typename T, typename Fn>
psi::pir::RawDatabase BuildItemDatabase(const YpirParameters& params,
                                        Fn&& make_value) {
  std::vector<std::vector<uint8_t>> rows(
      params.NumItems(), std::vector<uint8_t>(params.value_bytes, 0));
  for (uint64_t raw_idx = 0; raw_idx < params.NumItems(); ++raw_idx) {
    const T value = make_value(raw_idx);
    std::memcpy(rows[raw_idx].data(), &value, params.value_bytes);
  }
  return psi::pir::RawDatabase(std::move(rows));
}

TEST(YpirPirInterfaceTest, SimplepirImplementsIndexPirInterfaces) {
  auto params = CreateSmallTestParamsSimplePIR();
  YpirClient concrete_client(params);
  YpirServer<uint16_t> concrete_server(params);

  psi::pir::IndexPirClient& client = concrete_client;
  psi::pir::IndexPirDataBase& server = concrete_server;

  const auto raw_db = BuildItemDatabase<uint16_t>(
      params, [&](uint64_t raw_idx) {
        const uint64_t row = raw_idx / params.db_cols;
        const uint64_t col = raw_idx % params.db_cols;
        return static_cast<uint16_t>(
            (row * 17 + col * 3) % params.spiral_params.PtModulus());
      });
  server.GenerateFromRawData(raw_db);

  const uint64_t raw_idx = 123ULL * params.db_cols + 456ULL;
  const auto pks = client.GeneratePksBuffer();
  const auto query = client.GenerateIndexQuery(raw_idx);
  const auto response = server.Response(query, pks);
  const auto decoded = client.DecodeIndexResponse(response, raw_idx);

  EXPECT_EQ(client.GetPirType(), psi::pir::PirType::YPIR_PIR);
  EXPECT_EQ(server.GetPirType(), psi::pir::PirType::YPIR_PIR);
  EXPECT_EQ(server.MaxElementsOfOnePt(), 1U);
  EXPECT_TRUE(server.DbSeted());
  EXPECT_EQ(BytesToU64(decoded),
            (123ULL * 17 + 456ULL * 3) % params.spiral_params.PtModulus());
}

TEST(YpirPirInterfaceTest, DoublepirImplementsIndexPirInterfaces) {
  auto params = CreateSmallTestParamsDoublePIR();
  YpirClient concrete_client(params);
  YpirServer<uint8_t> concrete_server(params);

  psi::pir::IndexPirClient& client = concrete_client;
  psi::pir::IndexPirDataBase& server = concrete_server;

  const auto raw_db = BuildItemDatabase<uint8_t>(
      params, [&](uint64_t raw_idx) {
        const uint64_t row = raw_idx / params.db_cols;
        const uint64_t col = raw_idx % params.db_cols;
        return static_cast<uint8_t>((row + col) % 251);
      });
  server.GenerateFromRawData(raw_db);

  const uint64_t raw_idx = 111ULL * params.db_cols + 222ULL;
  const auto pks = client.GeneratePksBuffer();
  const auto query = client.GenerateIndexQuery(raw_idx);
  const auto response = server.Response(query, pks);
  const auto decoded = client.DecodeIndexResponse(response, raw_idx);

  EXPECT_EQ(client.GetPirType(), psi::pir::PirType::YPIR_PIR);
  EXPECT_EQ(server.GetPirType(), psi::pir::PirType::YPIR_PIR);
  EXPECT_EQ(server.MaxElementsOfOnePt(), 1U);
  EXPECT_TRUE(server.DbSeted());
  ASSERT_EQ(decoded.size(), 1U);
  EXPECT_EQ(decoded[0], static_cast<uint8_t>((111 + 222) % 251));
}

}  // namespace
}  // namespace psi::ypir
