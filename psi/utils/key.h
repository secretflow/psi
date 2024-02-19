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

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace psi {

// Multiple-Key out-of-core sort.
// Out-of-core support reference:
//   http://vkundeti.blogspot.com/2008/03/tech-algorithmic-details-of-unix-sort.html
// Multiple-Key support reference:
//   https://stackoverflow.com/questions/9471101/sort-csv-file-by-column-priority-using-the-sort-command
// use POSIX locale for sort
//   https://unix.stackexchange.com/questions/43465/whats-the-default-order-of-linux-sort/43466
//
// NOTE:
// This implementation requires `sort` command, which is guaranteed by our
// docker-way ship.
void MultiKeySort(const std::string& in_csv, const std::string& out_csv,
                  const std::vector<std::string>& keys,
                  bool numeric_sort = false, bool unique = false);

// join keys with ","
std::string KeysJoin(const std::vector<absl::string_view>& keys);

}  // namespace psi
