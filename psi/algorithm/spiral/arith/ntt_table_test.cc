// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/algorithm/spiral/arith/ntt_table.h"

#include <chrono>
#include <iostream>
#include <random>
#include <vector>

#include "gtest/gtest.h"

#include "psi/algorithm/spiral/arith/ntt.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral::arith {

namespace {

constexpr std::uint64_t kRefVal{519370102};

constexpr std::size_t kMaxLoop{100};

}  // namespace

TEST(NttTest, BuildNttTables) {
  std::vector<std::uint64_t> moduli{268369921ULL, 249561089ULL};
  std::size_t poly_len{2048};

  NttTables res = arith::BuildNttTables(poly_len, moduli);

  ASSERT_EQ(2, res.size());
  ASSERT_EQ(4, res[0].size());
  ASSERT_EQ(poly_len, res[0][0].size());

  ASSERT_EQ(134184961, res[0][2][0]);
  ASSERT_EQ(96647580, res[0][2][1]);

  std::uint64_t x1 = 0;
  for (std::size_t i = 0; i < res.size(); ++i) {
    for (std::size_t j = 0; j < res[0].size(); ++j) {
      for (std::size_t k = 0; k < res[0][0].size(); ++k) {
        x1 ^= res[i][j][k];
      }
    }
  }
  ASSERT_EQ(kRefVal, x1);
}

TEST(NttTest, NttForward) {
  auto params = util::GetFastExpansionTestingParam();

  std::vector<std::uint64_t> v1(2 * 2048, 0);
  v1[0] = 100;
  v1[2048] = 100;

  arith::NttForward(params, absl::MakeSpan(v1));
  ASSERT_EQ(v1[50], 100);
  ASSERT_EQ(v1[2048 + 50], 100);
}

TEST(NttTest, NttInverse) {
  auto params = util::GetFastExpansionTestingParam();

  std::vector<std::uint64_t> v1(2 * 2048, 100);
  arith::NttInverse(params, absl::MakeSpan(v1));
  ASSERT_EQ(v1[0], 100);
  ASSERT_EQ(v1[2048], 100);
  ASSERT_EQ(v1[50], 0);
  ASSERT_EQ(v1[2048 + 50], 0);
}

TEST(NttTest, NttCorrect) {
  auto params = util::GetFastExpansionTestingParam();

  std::vector<uint64_t> v1(params.CrtCount() * params.PolyLen());
  std::random_device rd;
  std::mt19937_64 prg(rd());

  uint64_t total_time = 0;

  for (size_t l = 0; l < kMaxLoop; ++l) {
    for (size_t i = 0; i < params.CrtCount(); ++i) {
      for (size_t j = 0; j < params.PolyLen(); ++j) {
        std::vector<std::size_t> indices{i, j};
        std::vector<size_t> lengths{params.CrtCount(), params.PolyLen()};
        auto idx = util::CalcIndex(indices, lengths);
        uint64_t val = prg();
        v1[idx] = val % params.Moduli(i);
      }
    }
    // copy
    std::vector<uint64_t> v2(v1.begin(), v1.end());

    auto start = std::chrono::high_resolution_clock::now();
    // forward
    arith::NttForward(params, absl::MakeSpan(v2));
    // inverse
    arith::NttInverse(params, absl::MakeSpan(v2));
    auto end = std::chrono::high_resolution_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    total_time += duration.count();

    ASSERT_EQ(v2, v1);
  }

  SPDLOG_INFO("{} Ntts, total time: {} micro-sec, each Ntt, time: {}", kMaxLoop,
              total_time, static_cast<double>(total_time) / kMaxLoop);
}

}  // namespace psi::spiral::arith
