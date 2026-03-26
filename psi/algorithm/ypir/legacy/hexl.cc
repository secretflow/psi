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

#include "psi/algorithm/ypir/legacy/hexl.h"

#include "yacl/base/exception.h"

namespace psi::ypir::ypir_internal {

YpirHexlNtt::YpirHexlNtt(uint64_t degree, uint64_t modulus,
                         uint64_t root_of_unity)
    : ntt_(degree, modulus, root_of_unity), degree_(degree), modulus_(modulus) {
  YACL_ENFORCE(degree_ > 0);
  YACL_ENFORCE(modulus_ > 0);
}

void YpirHexlNtt::Forward(uint64_t* data, size_t len) {
  YACL_ENFORCE(len == degree_, "NTT Forward expects len == degree ({} != {})",
               len, degree_);
  ntt_.ComputeForward(data, data, 1, 1);
}

void YpirHexlNtt::Inverse(uint64_t* data, size_t len) {
  YACL_ENFORCE(len == degree_, "NTT Inverse expects len == degree ({} != {})",
               len, degree_);
  ntt_.ComputeInverse(data, data, 1, 1);
}

void EltwiseMultMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                    size_t len, uint64_t modulus) {
  intel::hexl::EltwiseMultMod(out, a, b, len, modulus, 1);
}

void EltwiseAddMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                   size_t len, uint64_t modulus) {
  intel::hexl::EltwiseAddMod(out, a, b, len, modulus);
}

void EltwiseSubMod(uint64_t* out, const uint64_t* a, const uint64_t* b,
                   size_t len, uint64_t modulus) {
  intel::hexl::EltwiseSubMod(out, a, b, len, modulus);
}

void EltwiseFMAMod(uint64_t* out, const uint64_t* a, uint64_t scalar,
                   const uint64_t* add, size_t len, uint64_t modulus) {
  intel::hexl::EltwiseFMAMod(out, a, scalar, add, len, modulus, 1);
}

}  // namespace psi::ypir::ypir_internal
