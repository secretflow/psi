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

#include "psi/algorithm/spiral/discrete_gaussian.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {
namespace {

const std::vector<uint64_t> kCdfTable{0,
                                      0,
                                      0,
                                      7,
                                      225,
                                      6114,
                                      142809,
                                      2864512,
                                      49349166,
                                      730367088,
                                      9288667698,
                                      101545086850,
                                      954617134063,
                                      7720973857474,
                                      53757667977838,
                                      322436486442815,
                                      1667499996257363,
                                      7443566871362058,
                                      28720140744863912,
                                      95948302954529184,
                                      278161926109627936,
                                      701795634139702528,
                                      1546646853635105024,
                                      2991920295851131904,
                                      5112721055115152384,
                                      7782220156096217088,
                                      10664523917613334528ULL,
                                      13334023018594400256ULL,
                                      15454823777858420736ULL,
                                      16900097220074446848ULL,
                                      17744948439569850368ULL,
                                      18168582147599925248ULL,
                                      18350795770755024896ULL,
                                      18418023932964689920ULL,
                                      18439300506838192128ULL,
                                      18445076573713297408ULL,
                                      18446421637223112704ULL,
                                      18446690316041578496ULL,
                                      18446736352735698944ULL,
                                      18446743119092422656ULL,
                                      18446743972164470784ULL,
                                      18446744064420890624ULL,
                                      18446744072979191808ULL,
                                      18446744073660209152ULL,
                                      18446744073706694656ULL,
                                      18446744073709416448ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL,
                                      18446744073709551615ULL};

}

TEST(DiscreteGaussian, CdfTable) {
  DiscreteGaussian dg(6.4);
  ASSERT_EQ(dg.cdf_table_, kCdfTable);
}

TEST(DiscreteGaussian, Correct) {
  auto params = util::GetTestParam();
  DiscreteGaussian dg(params.NoiseWidth());

  yacl::crypto::Prg<uint64_t> prg(yacl::crypto::SecureRandU128());
  std::vector<int64_t> v;
  size_t trials = 10000;
  int64_t sum = 0;

  auto modulus = params.Modulus();
  for (size_t i = 0; i < trials; ++i) {
    auto val = dg.Sample(modulus, prg);
    auto val_i64 = static_cast<int64_t>(val);
    if (val_i64 >= (static_cast<int64_t>(modulus) / 2)) {
      val_i64 -= static_cast<int64_t>(modulus);
    }
    v.push_back(val_i64);
    sum += val_i64;
  }

  double computed_mean = static_cast<double>(sum) / trials;
  double expected_std_dev = params.NoiseWidth() / std::sqrt(2.0 * PI);
  double std_dev_of_mean =
      expected_std_dev / std::sqrt(static_cast<double>(trials));

  ASSERT_TRUE(std::abs(computed_mean) < std_dev_of_mean * 5.0);

  double computed_variance = 0.0;
  for (const auto& x : v) {
    computed_variance += std::pow(computed_mean - static_cast<double>(x), 2);
  }
  computed_variance = computed_variance / static_cast<double>(v.size());
  double computed_std_dev = std::sqrt(computed_variance);

  ASSERT_TRUE(std::abs(computed_std_dev - expected_std_dev) <
              expected_std_dev * 0.1);
}

}  // namespace psi::spiral
