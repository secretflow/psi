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

#include "psi/utils/batch_provider.h"

namespace psi {

class ArrowCsvBatchProvider : public IBasicBatchProvider,
                              public ILabeledBatchProvider {
 public:
  explicit ArrowCsvBatchProvider(const std::string& file_path,
                                 const std::vector<std::string>& keys,
                                 size_t batch_size = 1 << 20,
                                 const std::vector<std::string>& labels = {});

  std::vector<std::string> ReadNextBatch() override;

  std::pair<std::vector<std::string>, std::vector<std::string>>
  ReadNextLabeledBatch() override;

  [[nodiscard]] size_t row_cnt() const { return row_cnt_; }

  [[nodiscard]] size_t batch_size() const override { return batch_size_; }

 private:
  void Init();

  void ReadNextBatch(std::vector<std::string>* read_keys,
                     std::vector<std::string>* read_labels = nullptr);

  const size_t batch_size_;

  const std::string file_path_;

  const std::vector<std::string> keys_;

  const std::vector<std::string> labels_;

  size_t row_cnt_ = 0;

  std::shared_ptr<arrow::io::ReadableFile> infile_;

  std::shared_ptr<arrow::csv::StreamingReader> reader_;

  std::shared_ptr<arrow::RecordBatch> batch_;

  int64_t idx_in_batch_ = 0;

  std::vector<std::shared_ptr<arrow::StringArray>> arrays_;
};

}  // namespace psi
