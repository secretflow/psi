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

#include "psi/legacy/kmprt17_mp_psi/kmprt17_hashing.h"

#include <random>
#include <utility>

#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

namespace psi::psi {

void KmprtCuckooHashing::Insert(uint128_t elem) {
  auto insert_into = [this, &elem](uint8_t c) {
    for (uint8_t retry{}; retry != 128 && elem != NONE; ++retry) {
      uint8_t rand_idx = yacl::crypto::FastRandU64() % num_hashes_[c];
      uint8_t idx = (rand_idx + 1) % num_hashes_[c];
      size_t addr;
      do {
        addr = HashU128{}(elem, idx) % num_bins_[c];
        if (auto &bin = bins_[c][addr]; bin == NONE || bin == elem) {
          bin = std::exchange(elem, NONE);
          return;
        }
        idx = (idx + 1) % num_hashes_[c];
      } while (idx != rand_idx);
      std::swap(bins_[c][addr], elem);
    }
  };
  for (uint8_t c{}; c != 2; ++c) {
    insert_into(c);
  }
  YACL_ENFORCE_EQ(elem, NONE, "Failed to insert element.");
}

auto KmprtCuckooHashing::Lookup(uint128_t elem) const
    -> std::pair<uint8_t, size_t> {
  for (uint8_t c{}; c != 2; ++c) {
    for (uint8_t idx{}; idx != num_hashes_[c]; ++idx) {
      if (size_t addr = HashU128{}(elem, idx) % num_bins_[c];
          bins_[c][addr] == elem) {
        return {c, addr};
      }
    }
  }
  return {-1, -1};
}

void KmprtSimpleHashing::Insert(std::pair<uint128_t, uint64_t> point) {
  for (uint8_t c{}; c != 2; ++c) {
    for (size_t idx{}; idx != num_hashes_[c]; ++idx) {
      bins_[c][HashU128{}(point.first, idx) % num_bins_[c]].emplace(point);
    }
  }
}

}  // namespace psi::psi
