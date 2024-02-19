// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/rr22/okvs/aes_crhash.h"

#include <ostream>
#include <vector>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22::okvs {

class AesCrHashTest : public testing::TestWithParam<std::size_t> {};

TEST_P(AesCrHashTest, Works) {
  AesCrHash aes_crhash(yacl::MakeUint128(0xabc, 0x89ef));

  const size_t items_size = GetParam();

  std::vector<uint128_t> inputs(items_size);
  std::vector<uint128_t> outputs(items_size);
  std::vector<uint128_t> outputs2(items_size);
  std::vector<uint8_t> outputs3(items_size *
                                yacl::crypto::SymmetricCrypto::BlockSize());

  yacl::crypto::Prg<uint128_t> prng(yacl::MakeUint128(0x1234, 0x5678));
  prng.Fill(absl::MakeSpan(inputs));

  aes_crhash.Hash(absl::MakeSpan(inputs), absl::MakeSpan(outputs));

  for (size_t i = 0; i < items_size; ++i) {
    outputs2[i] = aes_crhash.Hash(inputs[i]);
  }

  aes_crhash.Hash(absl::MakeSpan((uint8_t *)(&inputs[0]), outputs3.size()),
                  absl::MakeSpan(outputs3));

  EXPECT_EQ(outputs, outputs2);
  EXPECT_EQ(std::memcmp(&outputs[0], &outputs3[0], outputs3.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, AesCrHashTest,
                         testing::Values(1, 2, 5, 10, 20));

}  // namespace psi::rr22::okvs
