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

#include "psi/rr22/okvs/baxos.h"

#include <ostream>
#include <vector>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

namespace psi::rr22::okvs {

class BaxosTest : public testing::TestWithParam<std::size_t> {};

TEST_P(BaxosTest, WORKS) {
  size_t items_num = GetParam();

  size_t bin_size = items_num / 4;
  size_t weight = 3;
  // statistical security parameter
  size_t ssp = 40;

  Baxos baxos;
  yacl::crypto::Prg<uint128_t> prng(yacl::crypto::FastRandU128());

  uint128_t seed;
  prng.Fill(absl::MakeSpan(&seed, 1));

  SPDLOG_INFO("items_num:{}, bin_size:{}", items_num, bin_size);

  baxos.Init(items_num, bin_size, weight, ssp, PaxosParam::DenseType::GF128,
             seed);

  SPDLOG_INFO("baxos.size(): {}", baxos.size());

  std::vector<uint128_t> items(items_num);
  std::vector<uint128_t> values(items_num);
  std::vector<uint128_t> values2(items_num);
  std::vector<uint128_t> p(baxos.size());

  prng.Fill(absl::MakeSpan(items.data(), items.size()));
  prng.Fill(absl::MakeSpan(values.data(), values.size()));

  baxos.Solve(absl::MakeSpan(items), absl::MakeSpan(values), absl::MakeSpan(p));

  baxos.Decode(absl::MakeSpan(items), absl::MakeSpan(values2),
               absl::MakeSpan(p));

  if (std::memcmp(values2.data(), values.data(),
                  values.size() * sizeof(uint128_t)) != 0) {
    for (uint64_t i = 0; i < items_num; ++i) {
      EXPECT_EQ(std::memcmp(&values[i], &values2[i], sizeof(uint128_t)), 0);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, BaxosTest,
                         testing::Values(16, 32, 64, 128, 2048));

}  // namespace psi::rr22::okvs
