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

#include "psi/rr22/okvs/paxos_hash.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

namespace psi::rr22::okvs {

class PaxosHashTest : public testing::TestWithParam<uint128_t> {};

TEST_P(PaxosHashTest, Works) {
  uint128_t seed = GetParam();

  yacl::crypto::Prg<uint128_t> prng(seed);

  PaxosHash<uint32_t> hasher;

  uint128_t paxos_seed;
  prng.Fill(absl::MakeSpan(&paxos_seed, 1));

  size_t weight = 3;

  size_t sparse_size = 10;

  hasher.init(seed, weight, sparse_size);

  uint128_t data;
  prng.Fill(absl::MakeSpan(&data, 1));

  uint128_t hash;

  std::vector<uint32_t> rows(weight);

  hasher.HashBuildRow1(data, absl::MakeSpan(rows), &hash);

  SPDLOG_INFO("hash: {}", hash);
  for (size_t i = 0; i < rows.size(); i++) {
    SPDLOG_INFO("[{}]={}", i, rows[i]);
  }
}

// yacl::MakeUint128(0x1234, 0x5678)

INSTANTIATE_TEST_SUITE_P(Works_Instances, PaxosHashTest,
                         testing::Values(yacl::MakeUint128(0x1234, 0x5678),
                                         yacl::crypto::FastRandU128()));

}  // namespace psi::rr22::okvs
