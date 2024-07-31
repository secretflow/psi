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

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "openssl/crypto.h"
#include "openssl/rand.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/ecc/ecc_spi.h"
#include "yacl/utils/parallel.h"

#include "psi/proto/psi.pb.h"

namespace psi {

inline constexpr int kEccKeySize = 32;

// Make ECDH implementation plugable.
class IEccCryptor {
 public:
  IEccCryptor() {
    YACL_ENFORCE(RAND_bytes(&private_key_[0], kEccKeySize) == 1,
                 "Cannot create random private key");
  }

  virtual ~IEccCryptor() { OPENSSL_cleanse(&private_key_[0], kEccKeySize); }

  virtual void SetPrivateKey(absl::Span<const uint8_t> key) {
    YACL_ENFORCE(key.size() == kEccKeySize);
    std::memcpy(private_key_, key.data(), key.size());
  }

  /// Get current curve type
  virtual CurveType GetCurveType() const = 0;

  virtual std::vector<yacl::crypto::EcPoint> EccMask(
      const std::vector<yacl::crypto::EcPoint>& points) const;

  yacl::crypto::EcPoint EccMask(const yacl::crypto::EcPoint& point,
                                const yacl::math::MPInt& sk) const;

  virtual size_t GetMaskLength() const;

  // Perform hash on input
  virtual yacl::crypto::EcPoint HashToCurve(
      absl::Span<const char> input) const = 0;

  [[nodiscard]] virtual yacl::Buffer SerializeEcPoint(
      const yacl::crypto::EcPoint& point) const {
    YACL_ENFORCE(ec_group_, "not implemented");
    return ec_group_->SerializePoint(point);
  }

  virtual yacl::crypto::EcPoint DeserializeEcPoint(
      yacl::ByteContainerView buf) const {
    YACL_ENFORCE(ec_group_, "not implemented");
    return ec_group_->DeserializePoint(buf);
  }

  std::vector<yacl::crypto::EcPoint> HashInputs(
      const std::vector<std::string>& items) const;

  std::vector<std::string> SerializeEcPoints(
      const std::vector<yacl::crypto::EcPoint>& points) const;

  std::vector<yacl::crypto::EcPoint> DeserializeEcPoints(
      const std::vector<std::string>& items) const;

  [[nodiscard]] const uint8_t* GetPrivateKey() const { return private_key_; }

 protected:
  uint8_t private_key_[kEccKeySize];
  std::unique_ptr<yacl::crypto::EcGroup> ec_group_ = nullptr;
};

}  // namespace psi
