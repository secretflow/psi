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

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/utils/io.h"

namespace psi {

class ScopedTempDir {
 public:
  bool CreateUniqueTempDirUnderPath(const std::filesystem::path& parent_path);

  const std::filesystem::path& path() const { return dir_; }

  ~ScopedTempDir() {
    if (!dir_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(dir_, ec);
      // Leave error as it is, do nothing
    }
  }

 private:
  std::filesystem::path dir_;
};

/// MultiplexDiskCache is a disk cache with multiple file storage.
class MultiplexDiskCache {
 public:
  explicit MultiplexDiskCache(const std::filesystem::path& path,
                              bool use_scoped_tmp_dir = true);

  MultiplexDiskCache(const MultiplexDiskCache&) = delete;
  MultiplexDiskCache& operator=(const MultiplexDiskCache&) = delete;

  std::string GetPath(size_t index) const;

  std::filesystem::path cache_dir() const { return cache_dir_; }

  void CreateOutputStreams(
      size_t num_bins,
      std::vector<std::unique_ptr<io::OutputStream>>* bin_outs) const;

  std::unique_ptr<io::InputStream> CreateInputStream(size_t index) const;

 private:
  std::filesystem::path cache_dir_;
  std::unique_ptr<ScopedTempDir> scoped_temp_dir_;
};

}  // namespace psi
