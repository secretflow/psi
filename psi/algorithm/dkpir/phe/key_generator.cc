// Copyright 2025
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

#include "psi/algorithm/dkpir/phe/key_generator.h"

namespace psi::dkpir::phe {
void KeyGenerator::GenerateKey(
    const std::shared_ptr<yacl::crypto::EcGroup>& curve, PublicKey* pk,
    SecretKey* sk) {
  yacl::math::MPInt k;
  yacl::math::MPInt::RandomLtN(curve->GetOrder(), &k);

  GenerateKey(curve, k, pk, sk);
}

void KeyGenerator::GenerateKey(
    const std::shared_ptr<yacl::crypto::EcGroup>& curve,
    const yacl::math::MPInt& x, PublicKey* pk, SecretKey* sk) {
  yacl::crypto::EcPoint pk_point = curve->MulBase(x);

  *sk = SecretKey(x, curve);
  *pk = PublicKey(pk_point, curve);
}
}  // namespace psi::dkpir::phe