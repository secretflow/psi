// Copyright 2024 The secretflow authors.
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
#include <tuple>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/link/link.h"

namespace psi::psi {
template <class FieldType>
void generateRandomShares(int numOfRnadoms,
                          std::vector<FieldType>& randomElementsToFill);
// Table-based OPPRF, see https://eprint.iacr.org/2017/799.pdf (Figure 6)

// prepare additive and T-threshold sharings of secret random value r_j using
// DN07's protocol template <class FieldType> void modDoubleRandom(uint64_t
// no_random, std::vector<FieldType>& randomElementsToFill);

// template <class FieldType>
// void generateRandom2TAndTShares(int numOfRandomPairs, std::vector<FieldType>&
// randomElementsToFill);

// template <class FieldType>
void randomParse(std::vector<uint128_t>& items, size_t count,
                 std::vector<std::vector<uint64_t>>& shares,
                 std::vector<std::shared_ptr<yacl::link::Context>> p2p,
                 size_t me_id, size_t wsize, int M, int N);

// evaluate circuit
// template <class FieldType>
// uint64_t evaluateCircuit();

// convert additive shares to T-threshold
// void additiveToThreshold();
// void mult_sj();

}  // namespace psi::psi
