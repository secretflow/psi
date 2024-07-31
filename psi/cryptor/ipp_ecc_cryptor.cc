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

#include "psi/cryptor/ipp_ecc_cryptor.h"

#include <algorithm>
#include <array>

#include "crypto_mb/x25519.h"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/cryptor/hash_to_curve_elligator2.h"

namespace psi {

using yacl::crypto::Array32;
using yacl::crypto::EcPoint;

std::vector<EcPoint> IppEccCryptor::EccMask(
    const std::vector<EcPoint> &points) const {
  std::array<const int8u *, 8> ptr_sk;
  std::fill(ptr_sk.begin(), ptr_sk.end(),
            static_cast<const int8u *>(&private_key_[0]));

  int8u key_data[8][32];  // Junk buffer

  auto mask_functor = [&ptr_sk, &key_data](absl::Span<const EcPoint> in,
                                           absl::Span<EcPoint> out) {
    size_t current_batch_size = in.size();

    std::array<const int8u *, 8> ptr_pk;
    std::array<int8u *, 8> ptr_key;

    for (size_t i = 0; i < 8; i++) {
      if (i < current_batch_size) {
        ptr_pk[i] = static_cast<const int8u *>(std::get<Array32>(in[i]).data());
        ptr_key[i] = static_cast<int8u *>(std::get<Array32>(out[i]).data());
      } else {
        ptr_pk[i] = static_cast<const int8u *>(std::get<Array32>(in[0]).data());
        ptr_key[i] = static_cast<int8u *>(key_data[i]);
      }
    }
    mbx_status status =
        mbx_x25519_mb8(ptr_key.data(), ptr_sk.data(), ptr_pk.data());
    YACL_ENFORCE(status == 0, "ippc mbx_x25519_mb8 Error: ", status);
  };

  std::vector<EcPoint> ret(points.size());
  yacl::parallel_for(0, points.size(), 8, [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; idx += 8) {
      int64_t current_batch_size = std::min(static_cast<int64_t>(8), end - idx);
      mask_functor(absl::MakeSpan(points).subspan(idx, current_batch_size),
                   absl::MakeSpan(ret).subspan(idx, current_batch_size));
    }
  });

  return ret;
}

yacl::crypto::EcPoint IppEccCryptor::HashToCurve(
    absl::Span<const char> input) const {
  return yacl::crypto::Sha256(input);
}

yacl::Buffer IppEccCryptor::SerializeEcPoint(
    const yacl::crypto::EcPoint &point) const {
  yacl::Buffer buf(kEccKeySize);
  memcpy(buf.data(), std::get<Array32>(point).data(), kEccKeySize);
  return buf;
}

yacl::crypto::EcPoint IppEccCryptor::DeserializeEcPoint(
    yacl::ByteContainerView buf) const {
  YACL_ENFORCE(buf.size() == kEccKeySize, "buf size {} not equal to {}",
               buf.size(), kEccKeySize);
  yacl::crypto::EcPoint p(std::in_place_type<Array32>);
  memcpy(std::get<Array32>(p).data(), buf.data(), buf.size());
  return p;
}

EcPoint IppElligator2Cryptor::HashToCurve(
    absl::Span<const char> item_data) const {
  return HashToCurveElligator2(item_data);
}

}  // namespace psi
