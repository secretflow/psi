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

#include "psi/cryptor/sodium_curve25519_cryptor.h"

#include <iostream>

#include "yacl/crypto/hash/hash_utils.h"

#include "psi/cryptor/hash_to_curve_elligator2.h"

namespace psi {

yacl::crypto::EcPoint SodiumCurve25519Cryptor::HashToCurve(
    absl::Span<const char> item_data) const {
  return yacl::crypto::Sha256(item_data);
  //  return ec_group_->HashToCurve(
  //      yacl::crypto::HashToCurveStrategy::HashAsPointX_SHA2,
  //      std::string_view(item_data.data(), item_data.size()));
}

std::vector<uint8_t> SodiumCurve25519Cryptor::KeyExchange(
    const std::shared_ptr<yacl::link::Context>& link_ctx) {
  yacl::math::MPInt sk(0, kEccKeySize * CHAR_BIT);
  sk.FromMagBytes(private_key_, yacl::Endian::little);
  auto self_public_key = ec_group_->MulBase(sk);
  link_ctx->SendAsyncThrottled(
      link_ctx->NextRank(), ec_group_->SerializePoint(self_public_key),
      fmt::format("send rank-{} public key", link_ctx->Rank()));
  yacl::Buffer peer_pubkey_buf = link_ctx->Recv(
      link_ctx->NextRank(),
      fmt::format("recv rank-{} public key", link_ctx->NextRank()));
  auto peer_public_key = ec_group_->DeserializePoint(peer_pubkey_buf);
  auto dh_key = ec_group_->Mul(peer_public_key, sk);
  const auto shared_key =
      yacl::crypto::Blake3(ec_group_->SerializePoint(dh_key));
  return {shared_key.begin(), shared_key.end()};
}

yacl::crypto::EcPoint SodiumElligator2Cryptor::HashToCurve(
    absl::Span<const char> item_data) const {
  return HashToCurveElligator2(item_data);
}

}  // namespace psi
