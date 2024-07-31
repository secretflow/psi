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

#include "psi/cryptor/ecc_cryptor.h"

#include <vector>

#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

namespace psi {

std::vector<yacl::crypto::EcPoint> IEccCryptor::EccMask(
    const std::vector<yacl::crypto::EcPoint>& points) const {
  yacl::math::MPInt sk(0, kEccKeySize * CHAR_BIT);
  sk.FromMagBytes(private_key_, yacl::Endian::little);

  std::vector<yacl::crypto::EcPoint> ret(points.size());
  yacl::parallel_for(0, points.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      ret[idx] = this->EccMask(points.at(idx), sk);
    }
  });
  return ret;
}

yacl::crypto::EcPoint IEccCryptor::EccMask(const yacl::crypto::EcPoint& point,
                                           const yacl::math::MPInt& sk) const {
  YACL_ENFORCE(ec_group_, "not implemented");
  return ec_group_->Mul(point, sk);
}

size_t IEccCryptor::GetMaskLength() const {
  YACL_ENFORCE(ec_group_, "not implemented");
  return ec_group_->GetSerializeLength();
}

std::vector<yacl::crypto::EcPoint> IEccCryptor::HashInputs(
    const std::vector<std::string>& items) const {
  std::vector<yacl::crypto::EcPoint> ret(items.size());
  yacl::parallel_for(0, items.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      ret[idx] = HashToCurve(items[idx]);
    }
  });
  return ret;
}

std::vector<std::string> IEccCryptor::SerializeEcPoints(
    const std::vector<yacl::crypto::EcPoint>& points) const {
  std::vector<std::string> ret(points.size());
  yacl::parallel_for(0, points.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      ret[idx] = this->SerializeEcPoint(points[idx]);
    }
  });
  return ret;
}

std::vector<yacl::crypto::EcPoint> IEccCryptor::DeserializeEcPoints(
    const std::vector<std::string>& items) const {
  std::vector<yacl::crypto::EcPoint> ret(items.size());
  yacl::parallel_for(0, items.size(), [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      ret[idx] = this->DeserializeEcPoint(items[idx]);
    }
  });
  return ret;
}

}  // namespace psi
