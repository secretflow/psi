// Copyright 2025 Ant Group Co., Ltd.
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

#include <cstdint>

namespace psi::api {

struct PsiExecuteReport {
  // The data count of input.
  int64_t original_count = -1;

  // The count of intersection. Get `-1` when self party can not get
  // result.
  int64_t intersection_count = -1;

  int64_t original_unique_count = -1;

  int64_t intersection_unique_count = -1;
};

}  // namespace psi::api
