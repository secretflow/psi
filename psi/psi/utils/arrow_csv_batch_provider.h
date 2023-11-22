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

#include <cstddef>
#include <string>

#include "arrow/csv/api.h"
#include "arrow/io/api.h"

#include "psi/psi/utils/batch_provider.h"

namespace psi::psi {

class ArrowCsvBatchProvider : public IBasicBatchProvider {
 public:
  // NOTE(junfeng): block_size is not col num of each batch, which by default is
  // 1 << 20 (1 Mb).
  explicit ArrowCsvBatchProvider(const std::string& file_path,
                                 const std::vector<std::string>& keys = {},
                                 const std::string& separator = ",",
                                 size_t block_size = 1 << 20);

  std::vector<std::string> ReadNextBatch() override;

  [[nodiscard]] size_t row_cnt() const { return row_cnt_; }

  [[nodiscard]] size_t batch_size() const { return block_size_; }

 private:
  void Init();

  const size_t block_size_;

  const std::string file_path_;

  const std::vector<std::string> keys_;

  const std::string separator_;

  size_t row_cnt_ = 0;

  std::shared_ptr<arrow::io::ReadableFile> infile_;

  std::shared_ptr<arrow::csv::StreamingReader> reader_;
};

}  // namespace psi::psi
