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

#pragma once

#include "heu/library/algorithms/elgamal/elgamal.h"

namespace psi::dkpir {

using namespace heu::lib::algorithms::elgamal;

// This is a rework of Encryptor in heu::lib::algorithms::elgamal. Since
// ElGamal's decryption will not be used in DkPir, the range restriction for
// plaintext has been removed.
class ElgamalEncryptor {
 public:
  explicit ElgamalEncryptor(const PublicKey &public_key)
      : public_key_(public_key) {
    Ciphertext::EnableEcGroup(public_key_.GetCurve());
  }

  // Remove the range check of plaintext
  Ciphertext Encrypt(const Plaintext &m) const {
    yacl::math::MPInt r;
    yacl::math::MPInt::RandomLtN(public_key_.GetCurve()->GetOrder(), &r);
    return Ciphertext(
        public_key_.GetCurve(), public_key_.GetCurve()->MulBase(r),
        public_key_.GetCurve()->MulDoubleBase(m, r, public_key_.GetH()));
  }

 private:
  PublicKey public_key_;
};
}  // namespace psi::dkpir