// Copyright 2025 Ant Group Co., Ltd.
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

#include <cstdint>

namespace psi::api {

// The specified elliptic curve cryptography used in psi.
enum class EllipticCurveType : std::uint8_t {
  CURVE_INVALID_TYPE = 0,

  // Daniel J. Bernstein. Curve25519: new diffie-hellman speed records
  CURVE_25519 = 1,

  // FourQ: four-dimensional decompositions on a Q-curve over the Mersenne prime
  CURVE_FOURQ = 2,

  // SM2 is an elliptic curve based cryptosystem (ECC)
  // published as a Chinese National Standard as GBT.32918.1-2016
  // and published in ISO/IEC 14888-3:2018
  CURVE_SM2 = 3,

  // parameters of the elliptic curve defined in Standards for Efficient
  // Cryptography (SEC) http://www.secg.org/sec2-v2.pdf
  CURVE_SECP256K1 = 4,

  // Curve25519 with rfc9380 elligator2 hash_to_curve
  CURVE_25519_ELLIGATOR2 = 5,

  // TODO: @changjun.zl support ristretto255
  // Ristretto255 implements abstract prime-order group interface of Curve25519
  // CURVE_RISTRETTO255 = 5;
};

}  // namespace psi::api
