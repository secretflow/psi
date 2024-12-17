// Copyright 2024 zhangwfjh
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

#include <tuple>
#include <unordered_map>
#include <vector>

#include "absl/numeric/int128.h"
#include "yacl/base/int128.h"

namespace psi::psi {

struct HashU128 {
  size_t operator()(uint128_t x, uint8_t idx = 0) const {
    return absl::Uint128High64(x) + idx * absl::Uint128Low64(x);
  }
};

template <typename Bin>
struct KmprtDoubleHashing {
  KmprtDoubleHashing(size_t m1, size_t m2) : num_bins_{m1, m2} {
    bins_[0].resize(m1);
    bins_[1].resize(m2);
  }

  const Bin &GetBin(uint8_t c, size_t addr) const { return bins_[c][addr]; }

  const uint8_t num_hashes_[2]{3, 2};
  const size_t num_bins_[2];
  std::vector<Bin> bins_[2];
};

class KmprtCuckooHashing : public KmprtDoubleHashing<uint128_t> {
 public:
  constexpr static uint128_t NONE{};

  KmprtCuckooHashing(size_t m1, size_t m2) : KmprtDoubleHashing{m1, m2} {}

  void Insert(uint128_t);
  std::pair<uint8_t, size_t> Lookup(uint128_t) const;
};

class KmprtSimpleHashing
    : public KmprtDoubleHashing<
          std::unordered_map<uint128_t, uint64_t, HashU128>> {
 public:
  KmprtSimpleHashing(size_t m1, size_t m2) : KmprtDoubleHashing{m1, m2} {}

  void Insert(std::pair<uint128_t, uint64_t>);
};

}  // namespace psi::psi
