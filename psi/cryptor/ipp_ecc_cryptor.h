// Copyright 2022 Ant Group Co., Ltd.
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

#include <vector>

#include "openssl/crypto.h"
#include "openssl/rand.h"
#include "yacl/base/exception.h"

#include "psi/cryptor/ecc_cryptor.h"

namespace psi {

class IppEccCryptor : public IEccCryptor {
 public:
  IppEccCryptor() = default;

  ~IppEccCryptor() override = default;

  CurveType GetCurveType() const override { return CurveType::CURVE_25519; }

  std::vector<yacl::crypto::EcPoint> EccMask(
      const std::vector<yacl::crypto::EcPoint>& points) const override;

  size_t GetMaskLength() const override { return kEccKeySize; }

  yacl::crypto::EcPoint HashToCurve(
      absl::Span<const char> item_data) const override;

  [[nodiscard]] yacl::Buffer SerializeEcPoint(
      const yacl::crypto::EcPoint& point) const override;

  yacl::crypto::EcPoint DeserializeEcPoint(
      yacl::ByteContainerView buf) const override;
};

class IppElligator2Cryptor : public IppEccCryptor {
  yacl::crypto::EcPoint HashToCurve(
      absl::Span<const char> item_data) const override;
};

}  // namespace psi
