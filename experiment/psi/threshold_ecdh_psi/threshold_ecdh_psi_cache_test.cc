// Copyright 2025
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

#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi_cache.h"

#include <filesystem>
#include <vector>

#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/utils/random_str.h"

namespace psi::ecdh {

TEST(ThresholdEcdhPsiCacheTest, Works) {
  auto uuid_str = GetRandomString();
  std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                   uuid_str};
  std::filesystem::create_directories(tmp_folder);

  // Generate test data.
  size_t data_count = 50;

  std::vector<uint32_t> origin_indexes(data_count);
  std::iota(origin_indexes.begin(), origin_indexes.end(), 0);

  std::vector<uint32_t> shuffled_indexes(data_count);
  std::iota(shuffled_indexes.begin(), shuffled_indexes.end(), 0);
  yacl::crypto::YaclReplayUrbg<uint32_t> gen(yacl::crypto::SecureRandSeed(),
                                             yacl::crypto::SecureRandU64());
  std::shuffle(shuffled_indexes.begin(), shuffled_indexes.end(), gen);

  // Save test data using `ThresholdEcdhPsiCache`
  ThresholdEcdhPsiCache cache(tmp_folder);

  for (size_t i = 0; i < data_count; ++i) {
    cache.SaveData(origin_indexes[i], shuffled_indexes[i]);
  }
  cache.Flush();

  // Read test data using `ThresholdEcdhPsiCacheProvider`
  ThresholdEcdhPsiCacheProvider provider(tmp_folder, data_count + 1);

  auto shuffled_batch = provider.ReadNextShuffledBatch();
  auto& batch_indices = shuffled_batch.batch_indices;
  auto& shuffled_indices = shuffled_batch.shuffled_indices;

  // Verify whether the two are equal.
  EXPECT_EQ(origin_indexes.size(), batch_indices.size());
  EXPECT_EQ(shuffled_indexes.size(), shuffled_indices.size());

  for (size_t i = 0; i < data_count; ++i) {
    EXPECT_EQ(origin_indexes[i], batch_indices[i]);
    EXPECT_EQ(shuffled_indexes[i], shuffled_indices[i]);
  }

  {
    std::error_code ec;
    std::filesystem::remove_all(tmp_folder, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                  tmp_folder.string(), ec.message());
    }
  }
}
}  // namespace psi::ecdh