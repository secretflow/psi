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

#include "psi/rr22/davis_meyer_hash.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

namespace psi::rr22 {

TEST(DavisMeyerHashTest, Works) {
  uint128_t seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint128_t> prg(seed);

  std::vector<uint128_t> key(20);
  std::vector<uint128_t> value(20);
  std::vector<uint128_t> outputs1(20);
  std::vector<uint128_t> outputs2(20);

  for (size_t i = 0; i < key.size(); i++) {
    key[i] = prg();
    value[i] = prg();

    outputs1[i] = DavisMeyerHash(key[i], value[i]);
    outputs2[i] = value[i];
  }

  DavisMeyerHash(absl::MakeSpan(key), absl::MakeSpan(outputs2),
                 absl::MakeSpan(outputs2));

  for (size_t i = 0; i < outputs1.size(); i++) {
    EXPECT_EQ(outputs1[i], outputs2[i]);
  }
}

}  // namespace psi::rr22
