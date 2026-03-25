// Copyright 2026 The secretflow authors.
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
#include <cstdint>

#include "hexl/hexl.hpp"

namespace psi::ypir::ypir_internal {

constexpr uint64_t kCrtQ1 = 268369921ULL;
constexpr uint64_t kCrtQ2 = 249561089ULL;
constexpr uint64_t kCrtMod = kCrtQ1 * kCrtQ2;
constexpr uint64_t kRootOfUnityCrt = 38878761190133527ULL;

class YpirHexlNtt {
 public:
  YpirHexlNtt(uint64_t degree, uint64_t modulus, uint64_t root_of_unity);

  void Forward(uint64_t* data, size_t len);
  void Inverse(uint64_t* data, size_t len);

  intel::hexl::NTT& Raw() { return ntt_; }

 private:
  intel::hexl::NTT ntt_;
  uint64_t degree_ = 0;
  uint64_t modulus_ = 0;
};

void EltwiseMultMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                    size_t len, uint64_t modulus);
void EltwiseAddMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                   size_t len, uint64_t modulus);
void EltwiseSubMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                   size_t len, uint64_t modulus);
void EltwiseFMAMod(uint64_t* out, const uint64_t* a, uint64_t scalar,
                   const uint64_t* add, size_t len, uint64_t modulus);

}  // namespace psi::ypir::ypir_internal
