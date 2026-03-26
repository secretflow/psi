#include "psi/algorithm/ypir/server.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

template <typename T>
psi::pir::RawDatabase BuildDatabase(const YpirParameters& params) {
  std::vector<std::vector<uint8_t>> rows(
      params.db_rows, std::vector<uint8_t>(params.db_cols * sizeof(T), 0));
  for (uint64_t row = 0; row < params.db_rows; ++row) {
    for (uint64_t col = 0; col < params.db_cols; ++col) {
      const T value = static_cast<T>((row * 17 + col * 3) % 251);
      std::memcpy(rows[row].data() + col * sizeof(T), &value, sizeof(T));
    }
  }
  return psi::pir::RawDatabase(std::move(rows));
}

TEST(YpirServerTest, SimplepirOfflineStateIsEmpty) {
  auto params = CreateSmallTestParamsSimplePIR();
  YpirServer<uint16_t> server(params);
  server.GenerateFromRawData(BuildDatabase<uint16_t>(params));

  const auto state = server.PerformOfflinePrecomputation();
  EXPECT_EQ(state.mode, YpirMode::kSimplepir);
  EXPECT_TRUE(state.hint_0.empty());
  EXPECT_TRUE(state.server_hint.empty());
  EXPECT_TRUE(state.decomp_buf.empty());
}

TEST(YpirServerTest, DoublepirOfflineStateCanBeReused) {
  auto params = CreateSmallTestParamsDoublePIR();
  YpirServer<uint8_t> server(params);
  server.GenerateFromRawData(BuildDatabase<uint8_t>(params));

  const auto state1 = server.PerformOfflinePrecomputation();
  const auto state2 = server.PerformOfflinePrecomputation();

  EXPECT_EQ(state1.mode, YpirMode::kDoublepir);
  EXPECT_EQ(state2.mode, YpirMode::kDoublepir);
  EXPECT_EQ(state1.hint_0, state2.hint_0);
  EXPECT_EQ(state1.server_hint, state2.server_hint);
}

}  // namespace
}  // namespace psi::ypir
