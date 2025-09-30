// Copyright 2025 The secretflow authors.
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
#include <vector>

namespace pir::ypir {
class YPIRServer {
 public:
  YPIRServer(size_t row_num, size_t col_num, size_t d1, size_t d2, uint64_t p, uint64_t q1, uint64_t q2, uint128_t seed);

  void gen_db();

 private:
  size_t row_num_;
  size_t col_num_;
  size_t d1_;
  size_t d2_;
  uint64_t p_;
  uint64_t q1_;
  uint64_t q2_;
  uint128_t seed_;
  uint64_t delta1_;
  uint64_t delta2_;
  std::vector<std::vector<uint64_t>> database_;
};

}  // namespace pir::ypir
