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

#include "psi/rr22/rr22_psi.h"

#include <cstdint>
#include <future>
#include <mutex>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/test_util.h"

#include "psi/rr22/rr22_utils.h"
#include "psi/utils/hash_bucket_cache.h"

namespace psi::rr22 {

namespace {

std::tuple<std::vector<uint128_t>, std::vector<uint128_t>,
           std::vector<uint32_t>>
GenerateTestData(size_t item_size, double p = 0.5) {
  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  std::vector<uint128_t> inputs_a(item_size);
  std::vector<uint128_t> inputs_b(item_size);

  prng.Fill(absl::MakeSpan(inputs_a));
  prng.Fill(absl::MakeSpan(inputs_b));

  std::mt19937 std_rand(yacl::crypto::FastRandU64());
  std::bernoulli_distribution dist(p);

  std::vector<uint32_t> indices;
  for (size_t i = 0; i < item_size; ++i) {
    if (dist(std_rand)) {
      inputs_b[i] = inputs_a[i];
      indices.push_back(i);
    }
  }
  return std::make_tuple(inputs_a, inputs_b, indices);
}

struct TestParams {
  uint64_t items_num;

  Rr22PsiMode mode = Rr22PsiMode::FastMode;

  bool malicious = false;
};

}  // namespace

class Rr22PsiTest : public testing::TestWithParam<TestParams> {};

TEST_P(Rr22PsiTest, CorrectTest) {
  auto params = GetParam();

  auto lctxs = yacl::link::test::SetupWorld("ab", 2);

  uint128_t seed = yacl::MakeUint128(0, 0);
  yacl::crypto::Prg<uint128_t> prng(seed);

  size_t item_size = params.items_num;

  std::vector<uint128_t> inputs_a;
  std::vector<uint128_t> inputs_b;
  std::vector<uint32_t> indices;

  std::tie(inputs_a, inputs_b, indices) = GenerateTestData(item_size);

  Rr22PsiOptions psi_options(40, 0, true);

  psi_options.mode = params.mode;
  psi_options.malicious = params.malicious;
  std::vector<uint32_t> indices_psi;
  PreProcessFunc receiver_pre_f = [&](size_t) {
    std::vector<HashBucketCache::BucketItem> bucket_items(inputs_a.size());
    for (size_t i = 0; i < inputs_a.size(); ++i) {
      bucket_items[i] = {.index = i,
                         .base64_data = fmt::format("{}", inputs_a[i])};
    }
    return bucket_items;
  };
  std::mutex mtx;
  PostProcessFunc receiver_post_f =
      [&](size_t, const std::vector<HashBucketCache::BucketItem>& bucket_items,
          const std::vector<uint32_t>& indices,
          const std::vector<uint32_t>& peer_dup_cnt) {
        SPDLOG_INFO("receiver_post_f: {}, {}", indices.size(),
                    peer_dup_cnt.size());
        std::unique_lock lock(mtx);
        for (size_t i = 0; i < indices.size(); ++i) {
          indices_psi.push_back(bucket_items[indices[i]].index);
          for (size_t j = 0; j < peer_dup_cnt[i]; ++j) {
            indices_psi.push_back(bucket_items[indices[i]].index);
          }
        }
      };
  PreProcessFunc sender_pre_f = [&](size_t) {
    std::vector<HashBucketCache::BucketItem> bucket_items(inputs_b.size());
    for (size_t i = 0; i < inputs_b.size(); ++i) {
      bucket_items[i] = {.index = i,
                         .base64_data = fmt::format("{}", inputs_b[i])};
    }
    return bucket_items;
  };
  size_t bucket_num = 1;
  PostProcessFunc sender_post_f =
      [&](size_t, const std::vector<HashBucketCache::BucketItem>&,
          const std::vector<uint32_t>&,
          const std::vector<uint32_t>&) { return; };
  auto psi_receiver_proc = std::async([&] {
    Rr22Runner runner(lctxs[0], psi_options, bucket_num, false, receiver_pre_f,
                      receiver_post_f);
    runner.AsyncRun(0, false);
  });

  auto psi_sender_proc = std::async([&] {
    Rr22Runner runner(lctxs[1], psi_options, bucket_num, false, sender_pre_f,
                      sender_post_f);
    runner.AsyncRun(0, true);
  });

  psi_sender_proc.get();
  psi_receiver_proc.get();
  std::sort(indices_psi.begin(), indices_psi.end());
  std::vector<uint32_t> indices_result;
  for (size_t i = 0; i < bucket_num; i++) {
    indices_result.insert(indices_result.end(), indices.begin(), indices.end());
  }
  std::sort(indices_result.begin(), indices_result.end());
  SPDLOG_INFO("{}?={}", indices.size(), indices_psi.size());
  EXPECT_EQ(indices_result.size(), indices_psi.size());
  EXPECT_EQ(indices_result, indices_psi);
}

INSTANTIATE_TEST_SUITE_P(
    CorrectTest_Instances, Rr22PsiTest,
    testing::Values(TestParams{1 << 17, Rr22PsiMode::FastMode},
                    TestParams{1 << 17, Rr22PsiMode::FastMode, true},
                    TestParams{1 << 17, Rr22PsiMode::LowCommMode}));
}  // namespace psi::rr22
