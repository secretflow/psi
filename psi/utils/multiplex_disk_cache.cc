// Copyright 2022 Ant Group Co., Ltd.
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

#include <unistd.h>

#include <chrono>
#include <ctime>
#include <memory>

#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

namespace psi {

bool ScopedTempDir::CreateUniqueTempDirUnderPath(
    const std::filesystem::path& parent_path) {
  int tries = 0;

  const int kMaxTries = 10;
  boost::uuids::random_generator uuid_generator;

  do {
    auto uuid_str = boost::uuids::to_string(uuid_generator());
    dir_ = parent_path / uuid_str;
    if (!std::filesystem::exists(dir_)) {
      return std::filesystem::create_directory(dir_);
    }
    tries++;

  } while (tries < kMaxTries);

  return false;
}

MultiplexDiskCache::MultiplexDiskCache(const std::filesystem::path& path,
                                       bool use_scoped_tmp_dir) {
  if (use_scoped_tmp_dir) {
    scoped_temp_dir_ = std::make_unique<ScopedTempDir>();
    YACL_ENFORCE(scoped_temp_dir_->CreateUniqueTempDirUnderPath(path));
    cache_dir_ = scoped_temp_dir_->path();
  } else {
    cache_dir_ = path;
  }
}

std::string MultiplexDiskCache::GetPath(size_t index) const {
  std::filesystem::path path = cache_dir_ / std::to_string(index);

  return path.string();
}

void MultiplexDiskCache::CreateOutputStreams(
    size_t num_bins,
    std::vector<std::unique_ptr<io::OutputStream>>* bin_outs) const {
  YACL_ENFORCE(num_bins != 0, "bad num_bins={}", num_bins);
  for (size_t i = 0; i < num_bins; ++i) {
    // trunc is off.
    bin_outs->push_back(
        io::BuildOutputStream(io::FileIoOptions(GetPath(i), false, true)));
  }
}

std::unique_ptr<io::InputStream> MultiplexDiskCache::CreateInputStream(
    size_t index) const {
  return io::BuildInputStream(io::FileIoOptions(GetPath(index)));
}

}  // namespace psi
