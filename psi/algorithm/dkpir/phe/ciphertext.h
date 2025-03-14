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

#include "yacl/crypto/ecc/ec_point.h"
#include "yacl/crypto/ecc/ecc_spi.h"

namespace psi::dkpir::phe {
class Ciphertext {
 public:
  Ciphertext() = default;
  Ciphertext(const yacl::crypto::EcPoint& c0, const yacl::crypto::EcPoint& c1)
      : c0_(c0), c1_(c1) {}

  const yacl::crypto::EcPoint& GetC0() const { return c0_; }
  const yacl::crypto::EcPoint& GetC1() const { return c1_; }

  void SerializeCiphertext(const std::shared_ptr<yacl::crypto::EcGroup>& curve,
                           uint8_t* buf, uint64_t buf_size) const;
  void DeserializeCiphertext(
      const std::shared_ptr<yacl::crypto::EcGroup>& curve, uint8_t* buf,
      uint64_t buf_size);

 private:
  yacl::crypto::EcPoint c0_;
  yacl::crypto::EcPoint c1_;
};
}  // namespace psi::dkpir::phe