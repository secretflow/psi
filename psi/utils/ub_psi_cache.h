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

#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "yacl/base/byte_container_view.h"

#include "psi/utils/batch_provider.h"
#include "psi/utils/io.h"

namespace psi {

class UbPsiCacheProvider : public IBasicBatchProvider,
                           public IShuffledBatchProvider {
 public:
  UbPsiCacheProvider(const std::string &file_path, size_t batch_size,
                     size_t data_len);
  ~UbPsiCacheProvider() override { in_->Close(); }

  std::vector<std::string> ReadNextBatch() override;

  std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>>
  ReadNextShuffledBatch() override;

  const std::vector<std::string> &GetSelectedFields();

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  std::vector<std::tuple<std::string, size_t, size_t>> ReadData(
      size_t read_count);

  const size_t batch_size_;
  std::string file_path_;
  size_t file_size_;
  size_t file_cursor_ = 0;
  std::unique_ptr<io::InputStream> in_;
  size_t data_len_;
  size_t data_index_len_;

  std::vector<std::string> selected_fields_;
};

class IUbPsiCache {
 public:
  virtual ~IUbPsiCache() = default;

  virtual void SaveData(yacl::ByteContainerView item, size_t index,
                        size_t shuffle_index) = 0;

  virtual void Flush() { return; }
};

class UbPsiCache : public IUbPsiCache {
 public:
  UbPsiCache(const std::string &file_path, size_t data_len,
             const std::vector<std::string> &ids);

  ~UbPsiCache() { out_stream_->Close(); }

  void SaveData(yacl::ByteContainerView item, size_t index,
                size_t shuffle_index) override;

  void Flush() override { out_stream_->Flush(); }

 private:
  std::string file_path_;
  size_t data_len_;
  size_t data_index_len_;
  std::unique_ptr<io::OutputStream> out_stream_;
};

}  // namespace psi
