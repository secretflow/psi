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

#include "psi/algorithm/rr22/okvs/paxos.h"

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

namespace psi::rr22::okvs {

TEST(PaxosTest, SolveU64Test) {
  for (auto dt : {PaxosParam::DenseType::Binary}) {
    SPDLOG_INFO("=== dt:{}",
                dt == PaxosParam::DenseType::Binary ? "binary" : "gf128");

    [[maybe_unused]] uint64_t n = 1 << 12;

    [[maybe_unused]] uint64_t w = 3;
    [[maybe_unused]] uint64_t s = 0;
    uint64_t t = 1;

    for (uint64_t tt = 0; tt < t; ++tt) {
      // SPDLOG_INFO("=== tt:{} t:{}", tt, t);
      Paxos<uint32_t> paxos;

      [[maybe_unused]] uint128_t paxos_seed = yacl::crypto::SecureRandU128();
      SPDLOG_INFO("paxos_seed: {}", paxos_seed);

      paxos.Init(n, w, 40, dt, paxos_seed);

      std::vector<uint128_t> items(n);
      std::vector<uint64_t> values(n);
      std::vector<uint64_t> values2(n);
      std::vector<uint64_t> p(paxos.size());

      yacl::crypto::Prg<uint128_t> prng128(yacl::MakeUint128(tt, s));
      yacl::crypto::Prg<uint64_t> prng64(yacl::MakeUint128(tt, s));

      prng128.Fill(absl::MakeSpan(items.data(), items.size()));
      prng64.Fill(absl::MakeSpan(values.data(), values.size()));

      paxos.SetInput(absl::MakeSpan(items));

      SPDLOG_INFO("===encode===");
      paxos.EncodeU64(absl::MakeSpan(values), absl::MakeSpan(p));
      SPDLOG_INFO("===decode===");
      paxos.DecodeU64(absl::MakeSpan(items), absl::MakeSpan(values2),
                      absl::MakeSpan(p));
      EXPECT_EQ(std::memcmp(values2.data(), values.data(),
                            sizeof(uint64_t) * values.size()),
                0);
    }
  }
}

TEST(PaxosTest, SolveTest) {
  for (auto dt :
       {PaxosParam::DenseType::Binary, PaxosParam::DenseType::GF128}) {
    SPDLOG_INFO("=== dt:{}",
                dt == PaxosParam::DenseType::Binary ? "Binary" : "GF128");

    [[maybe_unused]] uint64_t n = 1 << 12;

    [[maybe_unused]] uint64_t w = 3;
    [[maybe_unused]] uint64_t s = 0;
    uint64_t t = 1;

    for (uint64_t tt = 0; tt < t; ++tt) {
      // SPDLOG_INFO("=== tt:{} t:{}", tt, t);
      Paxos<uint32_t> paxos;
      [[maybe_unused]] uint128_t paxos_seed = yacl::crypto::SecureRandU128();
      SPDLOG_INFO("paxos_seed: {}", paxos_seed);

      paxos.Init(n, w, 40, dt, paxos_seed);

      std::vector<uint128_t> items(n);
      std::vector<uint128_t> values(n);
      std::vector<uint128_t> values2(n);
      std::vector<uint128_t> p(paxos.size());
      yacl::crypto::Prg<uint128_t> prng(yacl::MakeUint128(tt, s));

      prng.Fill(absl::MakeSpan(items.data(), items.size()));
      prng.Fill(absl::MakeSpan(values.data(), values.size()));

      paxos.SetInput(absl::MakeSpan(items));
      SPDLOG_INFO("===encode===");
      paxos.Encode(absl::MakeSpan(values), absl::MakeSpan(p));
      SPDLOG_INFO("===decode===");
      paxos.Decode(absl::MakeSpan(items), absl::MakeSpan(values2),
                   absl::MakeSpan(p));

      EXPECT_EQ(std::memcmp(values2.data(), values.data(),
                            sizeof(uint128_t) * values.size()),
                0);
    }
  }
}

}  // namespace psi::rr22::okvs
