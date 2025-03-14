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

#include "yacl/crypto/ecc/ecc_spi.h"
namespace psi::dkpir::phe {
class SecretKey {
 public:
  SecretKey() = default;
  SecretKey(const yacl::math::MPInt& x,
            const std::shared_ptr<yacl::crypto::EcGroup>& curve)
      : x_(x), curve_(curve) {}

  const yacl::math::MPInt& GetSk() const { return x_; }
  const std::shared_ptr<yacl::crypto::EcGroup>& GetCurve() const {
    return curve_;
  }

 private:
  // private key sk = x_
  yacl::math::MPInt x_;
  std::shared_ptr<yacl::crypto::EcGroup> curve_;
};
}  // namespace psi::dkpir::phe