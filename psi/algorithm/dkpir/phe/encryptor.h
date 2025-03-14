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

#include "psi/algorithm/dkpir/phe/ciphertext.h"
#include "psi/algorithm/dkpir/phe/public_key.h"
namespace psi::dkpir::phe {
class Encryptor {
 public:
  explicit Encryptor(const PublicKey& pk) : pk_(pk), curve_(pk.GetCurve()) {}
  Ciphertext Encrypt(const yacl::math::MPInt& m) const;
  Ciphertext EncryptZero() const;

 private:
  PublicKey pk_;
  std::shared_ptr<yacl::crypto::EcGroup> curve_;
};
}  // namespace psi::dkpir::phe
