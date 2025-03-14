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

#include "psi/algorithm/dkpir/phe/encryptor.h"

namespace psi::dkpir::phe {
Ciphertext Encryptor::Encrypt(const yacl::math::MPInt& m) const {
  yacl::math::MPInt r;
  yacl::math::MPInt::RandomLtN(curve_->GetOrder(), &r);

  // c0 = r * G, c1 = m * G + r * PK
  yacl::crypto::EcPoint c0 = curve_->MulBase(r);
  yacl::crypto::EcPoint c1 = curve_->MulDoubleBase(m, r, pk_.GetPk());

  return Ciphertext(c0, c1);
}

Ciphertext Encryptor::EncryptZero() const {
  return Encrypt(yacl::math::MPInt(0));
}
}  // namespace psi::dkpir::phe