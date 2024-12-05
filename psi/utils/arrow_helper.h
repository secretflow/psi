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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "arrow/csv/api.h"

namespace psi {

#define PSI_ARROW_GET_RESULT(value, maker)                                   \
  do {                                                                       \
    auto result = maker;                                                     \
    YACL_ENFORCE(result.ok(), "Arrow result failed: {}, status: {}", #maker, \
                 result.status().message());                                 \
    value = std::move(*result);                                              \
  } while (0)

std::shared_ptr<arrow::csv::StreamingReader> MakeCsvReader(
    const std::string& path,
    const std::vector<std::string>& choosed_columns = {});

std::shared_ptr<arrow::csv::StreamingReader> MakeCsvReader(
    const std::string& path, std::shared_ptr<arrow::Schema> schema);

std::vector<std::string> GetCsvColumnsNames(const std::string& path);

}  // namespace psi
