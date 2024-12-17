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

#include "psi/algorithm/spiral/poly_matrix.h"

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include <cstdint>
#include <vector>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/common.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {

constexpr size_t kMaxLoop = 10;

}  // namespace

TEST(PolyMatrixRaw, Zero) {
  auto params = util::GetFastExpansionTestingParam();
  PolyMatrixRaw poly = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
  auto data = poly.Data();
  ASSERT_TRUE(std::all_of(data.begin(), data.end(),
                          [](uint64_t value) { return value == 0; }));
}

TEST(PolyMatrixRaw, CopyInto) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw poly = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
  std::vector<uint64_t> data(params.PolyLen() * 2 * 1, 1);
  PolyMatrixRaw poly2(params.PolyLen(), 2, 1, std::move(data));

  ASSERT_TRUE(poly != poly2);

  poly.CopyInto(poly2, 0, 0);
  ASSERT_TRUE(poly == poly2);
}

TEST(PolyMatrixRaw, SubMatrix) {
  auto params = util::GetFastExpansionTestingParam();
  std::vector<uint64_t> data(params.PolyLen() * 2 * 1, 1);
  PolyMatrixRaw poly(params.PolyLen(), 2, 1, std::move(data));

  auto sub = poly.SubMatrix(0, 0, 2, 1);
  ASSERT_TRUE(poly == sub);

  auto sub2 = poly.SubMatrix(0, 0, 1, 1);
  ASSERT_TRUE(poly != sub2);
}

TEST(PolyMatrixRaw, PadTop) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw poly = PolyMatrixRaw::Zero(params.PolyLen(), 4, 1);
  PolyMatrixRaw poly2 = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
  auto padded = poly2.PadTop(2);

  ASSERT_TRUE(padded == poly);
}

TEST(PolyMatrixRaw, Identity) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw poly = PolyMatrixRaw::Identity(params.PolyLen(), 2, 2);
  // verify
  std::vector<uint64_t> zero(params.PolyLen(), 0);
  std::vector<uint64_t> one(params.PolyLen(), 0);
  one[0] = 1;

  absl::Span<uint64_t> zero_span = absl::MakeSpan(zero);
  absl::Span<uint64_t> one_span = absl::MakeSpan(one);
  for (size_t r = 0; r < 2; ++r) {
    for (size_t c = 0; c < 2; ++c) {
      if (r == c) {
        ASSERT_TRUE(poly.Poly(r, c) == one_span);
      } else {
        ASSERT_TRUE(poly.Poly(r, c) == zero_span);
      }
    }
  }
}

TEST(PolyMatrixRaw, Reset) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw poly = PolyMatrixRaw::Identity(params.PolyLen(), 1, 1);
  poly.Reset(params.PolyLen(), 2, 1);
  PolyMatrixRaw poly2 = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);

  ASSERT_TRUE(poly == poly2);
}

TEST(PolyMatrixRaw, SingleValue) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw poly = PolyMatrixRaw::SingleValue(params.PolyLen(), 1024);
  ASSERT_EQ(poly.Rows(), 1);
  ASSERT_EQ(poly.Cols(), 1);
  ASSERT_EQ(poly.Data()[0], 1024);
}

TEST(PolyMatrixRaw, Clone) {
  auto params = util::GetFastExpansionTestingParam();
  PolyMatrixRaw poly = PolyMatrixRaw::Random(params, 2, 1);

  PolyMatrixRaw poly2(poly);
  ASSERT_TRUE(poly2 == poly);

  PolyMatrixRaw poly3 = poly;
  ASSERT_TRUE(poly3 == poly);

  PolyMatrixRaw poly4;
  poly4 = poly;
  ASSERT_TRUE(poly4 == poly);
}

TEST(PolyMatrixNtt, NumWords) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixNtt poly =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);

  ASSERT_EQ(poly.NumWords(), params.PolyLen() * params.CrtCount());
}

TEST(PolyMatrixNtt, MulZero) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixNtt m1 =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 3, 2);
  PolyMatrixNtt m2 = PolyMatrixNtt::Random(params, 2, 1);

  auto m3 = Multiply(params, m1, m2);

  for (size_t i = 0; i < m3.Data().size(); ++i) {
    ASSERT_EQ(0, m3.Data()[i]);
  }
}

TEST(PolyMatrixNtt, RawToNtt) {
  auto params = util::GetFastExpansionTestingParam();

  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto m = PolyMatrixRaw::Random(params, 2, 1);

    auto m_ntt = ToNtt(params, m);

    auto m_raw = FromNtt(params, m_ntt);

    ASSERT_EQ(m, m_raw);
  }
}

TEST(PolyMatrixNtt, NttToRaw) {
  auto params = util::GetFastExpansionTestingParam();

  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto m = PolyMatrixNtt::Random(params, 2, 1);
    auto m_raw = FromNtt(params, m);
    auto m_ntt = ToNtt(params, m_raw);
    ASSERT_EQ(m, m_ntt);
  }
}

TEST(PolyMatrixNtt, Multiply) {
  auto params = util::GetFastExpansionTestingParam();

  PolyMatrixRaw m1 = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  PolyMatrixRaw m2 = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);

  m1.Data()[m1.PolyStartIndex(0, 0) + 1] = 100;
  m2.Data()[m2.PolyStartIndex(0, 0) + 1] = 7;

  auto m1_ntt = ToNtt(params, m1);
  auto m2_ntt = ToNtt(params, m2);
  auto m3_ntt = Multiply(params, m1_ntt, m2_ntt);

  auto m3 = FromNtt(params, m3_ntt);

  ASSERT_EQ(700, m3.Data()[m3.PolyStartIndex(0, 0) + 2]);
  // other coeff = 0
  std::vector<uint64_t> expected(m3.Data().size(), 0);
  expected[2] = 700;
  ASSERT_EQ(m3.Data(), expected);
}
}  // namespace psi::spiral
