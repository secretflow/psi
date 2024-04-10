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

#include "psi/rr22/davis_meyer_hash.h"

#include "yacl/crypto/aes/aes_opt.h"
#include "yacl/crypto/block_cipher/symmetric_crypto.h"
#include "yacl/utils/platform_utils.h"

namespace psi::rr22 {

namespace {

// vector aes batch size
[[maybe_unused]] constexpr size_t kEncBatch = 8;

}  // namespace

uint128_t DavisMeyerHash(uint128_t key, uint128_t value) {
  yacl::crypto::SymmetricCrypto aes(
      yacl::crypto::SymmetricCrypto::CryptoType::AES128_ECB, key, 0);

  uint128_t output = aes.Encrypt(value);

  return output ^ value;
}

void DavisMeyerHash(absl::Span<const uint128_t> key,
                    absl::Span<const uint128_t> value,
                    absl::Span<uint128_t> outputs) {
  YACL_ENFORCE(key.size() == value.size());

#ifdef __x86_64__
  if (yacl::hasAVX2()) {
    size_t i = 0;

    yacl::crypto::AES_KEY aes_key[kEncBatch];

    std::array<uint128_t, kEncBatch> para_keys;
    std::array<uint128_t, kEncBatch> para_values;

    size_t batch_size = key.size() / kEncBatch * kEncBatch;

    // use Vector AES accelerate DavisMeyerHash
    // Ref: Vector AES Instructions for Security Applications eprint 2021-1493
    for (i = 0; i < batch_size; i += kEncBatch) {
      std::memcpy(para_keys.data(), &key[i], kEncBatch * sizeof(uint128_t));
      std::memcpy(para_values.data(), &value[i], kEncBatch * sizeof(uint128_t));
      yacl::crypto::AES_opt_key_schedule<kEncBatch>(para_keys.data(), aes_key);

      yacl::crypto::ParaEnc<kEncBatch, 1>(para_values.data(), aes_key);

      outputs[i] = para_values[0] ^ value[i];
      outputs[i + 1] = para_values[1] ^ value[i + 1];
      outputs[i + 2] = para_values[2] ^ value[i + 2];
      outputs[i + 3] = para_values[3] ^ value[i + 3];
      outputs[i + 4] = para_values[4] ^ value[i + 4];
      outputs[i + 5] = para_values[5] ^ value[i + 5];
      outputs[i + 6] = para_values[6] ^ value[i + 6];
      outputs[i + 7] = para_values[7] ^ value[i + 7];
    }
    for (; i < key.size(); ++i) {
      outputs[i] = DavisMeyerHash(key[i], value[i]);
    }
  } else
#endif
  {
    for (size_t i = 0; i < key.size(); ++i) {
      outputs[i] = DavisMeyerHash(key[i], value[i]);
    }
  }
}

}  // namespace psi::rr22
