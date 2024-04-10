// Copyright 2023 Ant Group Co., Ltd.
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

#include "yacl/base/byte_container_view.h"
#include "yacl/base/int128.h"
#include "yacl/crypto/block_cipher/symmetric_crypto.h"

// Correlation robust hash function.
// H(x) = AES(x) + x.
namespace psi::rr22::okvs {

class AesCrHash : public yacl::crypto::SymmetricCrypto {
 public:
  AesCrHash(uint128_t key, uint128_t iv = 0)
      : yacl::crypto::SymmetricCrypto(
            yacl::crypto::SymmetricCrypto::CryptoType::AES128_ECB, key, iv) {}

  AesCrHash(yacl::ByteContainerView key, yacl::ByteContainerView iv)
      : yacl::crypto::SymmetricCrypto(
            yacl::crypto::SymmetricCrypto::CryptoType::AES128_ECB, key, iv) {}

  void Hash(absl::Span<const uint8_t> plaintext,
            absl::Span<uint8_t> ciphertext) const;

  void Hash(absl::Span<const uint128_t> plaintext,
            absl::Span<uint128_t> ciphertext) const;

  uint128_t Hash(uint128_t input) const;
};

}  // namespace psi::rr22::okvs
