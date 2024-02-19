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

#include "psi/utils/multiplex_disk_cache.h"

#include <filesystem>

#include "gtest/gtest.h"

#include "psi/utils/io.h"

namespace psi {

std::size_t GetFileCntInDirectory(const std::filesystem::path& path) {
  using std::filesystem::directory_iterator;
  return std::distance(directory_iterator(path), directory_iterator{});
}

class MultiplexDiskCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = "./tmp";
    std::filesystem::create_directory(tmp_dir_);
  }
  void TearDown() override {
    if (!tmp_dir_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(tmp_dir_, ec);
      // Leave error as it is, do nothing
    }
  }

  std::string tmp_dir_;
};

TEST_F(MultiplexDiskCacheTest, UseScopedTmpDirOn) {
  std::filesystem::path cache_path;
  {
    MultiplexDiskCache disk_cache_(tmp_dir_);

    std::vector<std::unique_ptr<io::OutputStream>> output_streams;
    disk_cache_.CreateOutputStreams(10, &output_streams);
    for (int i = 0; i < 10; i++) {
      output_streams[i]->Flush();
    }

    cache_path = disk_cache_.cache_dir();
    EXPECT_EQ(10, GetFileCntInDirectory(cache_path));
  }

  EXPECT_EQ(0, GetFileCntInDirectory(tmp_dir_));
  EXPECT_FALSE(std::filesystem::exists(cache_path));
}

TEST_F(MultiplexDiskCacheTest, UseScopedTmpDirOff) {
  std::filesystem::path cache_path;
  {
    MultiplexDiskCache disk_cache_(tmp_dir_, false);

    std::vector<std::unique_ptr<io::OutputStream>> output_streams;
    disk_cache_.CreateOutputStreams(10, &output_streams);
    for (int i = 0; i < 10; i++) {
      output_streams[i]->Flush();
    }
    cache_path = disk_cache_.cache_dir();
    EXPECT_EQ(10, GetFileCntInDirectory(cache_path));
  }

  EXPECT_EQ(10, GetFileCntInDirectory(tmp_dir_));
  EXPECT_TRUE(std::filesystem::exists(cache_path));
  EXPECT_EQ(10, GetFileCntInDirectory(cache_path));
}

}  // namespace psi
