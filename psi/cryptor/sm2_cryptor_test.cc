// Copyright 2022 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "psi/cryptor/sm2_cryptor.h"

#include <future>
#include <iostream>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/tools/prg.h"

namespace psi {

struct TestParams {
  size_t items_size;
  CurveType type = CurveType::CURVE_SM2;
};

class Sm2CryptorTest : public ::testing::TestWithParam<TestParams> {};

TEST_P(Sm2CryptorTest, Works) {
  auto params = GetParam();
  std::random_device rd;
  yacl::crypto::Prg<uint64_t> prg(rd());

  std::shared_ptr<Sm2Cryptor> sm2_cryptor_a =
      std::make_shared<Sm2Cryptor>(params.type);
  std::shared_ptr<Sm2Cryptor> sm2_cryptor_b =
      std::make_shared<Sm2Cryptor>(params.type);

  std::string items_a(params.items_size * kEccKeySize, '\0');
  std::string items_b(params.items_size * kEccKeySize, '\0');

  prg.Fill(absl::MakeSpan(items_a.data(), items_a.length()));

  items_b = items_a;

  std::string items_a_point(params.items_size * (kEccKeySize + 1), '\0');
  std::string items_b_point(params.items_size * (kEccKeySize + 1), '\0');
  std::vector<yacl::crypto::EcPoint> points_a;
  std::vector<yacl::crypto::EcPoint> points_b;

  for (size_t idx = 0; idx < params.items_size; ++idx) {
    absl::Span<const char> items_span =
        absl::MakeSpan(&items_a[idx * kEccKeySize], kEccKeySize);
    yacl::crypto::EcPoint point = sm2_cryptor_a->HashToCurve(items_span);
    points_a.push_back(point);

    items_span = absl::MakeSpan(&items_b[idx * kEccKeySize], kEccKeySize);
    point = sm2_cryptor_b->HashToCurve(items_span);
    points_b.push_back(point);
  }

  // g^a
  auto masked_a = sm2_cryptor_a->EccMask(points_a);

  // (g^a)^b
  auto masked_ab = sm2_cryptor_b->EccMask(masked_a);

  // g^b
  auto masked_b = sm2_cryptor_b->EccMask(points_b);

  // (g^b)^a
  auto masked_ba = sm2_cryptor_a->EccMask(masked_b);

  EXPECT_EQ(sm2_cryptor_b->SerializeEcPoints(masked_ab),
            sm2_cryptor_a->SerializeEcPoints(masked_ba));
}

INSTANTIATE_TEST_SUITE_P(
    Works_Instances, Sm2CryptorTest,
    testing::Values(TestParams{1}, TestParams{10}, TestParams{50},
                    TestParams{100},
                    // CURVE_SECP256K1
                    TestParams{1, CurveType::CURVE_SECP256K1},
                    TestParams{10, CurveType::CURVE_SECP256K1},
                    TestParams{50, CurveType::CURVE_SECP256K1},
                    TestParams{100, CurveType::CURVE_SECP256K1}));

}  // namespace psi