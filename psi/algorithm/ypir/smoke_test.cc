#include "psi/algorithm/ypir/client.h"
#include "psi/algorithm/ypir/server.h"

#include "gtest/gtest.h"

namespace psi::ypir {
namespace {

TEST(YpirSmokeTest, ConstructPublicObjects) {
  auto simple_params = CreateSmallTestParamsSimplePIR();
  auto double_params = CreateSmallTestParamsDoublePIR();

  YpirClient simple_client(simple_params);
  YpirClient double_client(double_params);
  YpirServer<uint16_t> simple_server(simple_params);
  YpirServer<uint8_t> double_server(double_params);

  EXPECT_EQ(simple_client.GetParameters().mode, YpirMode::kSimplepir);
  EXPECT_EQ(double_client.GetParameters().mode, YpirMode::kDoublepir);
  EXPECT_FALSE(simple_server.DbSet());
  EXPECT_FALSE(double_server.DbSet());
}

}  // namespace
}  // namespace psi::ypir
