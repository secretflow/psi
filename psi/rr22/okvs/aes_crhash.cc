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

#include "psi/rr22/okvs/aes_crhash.h"

#include <vector>

#include "spdlog/spdlog.h"
#include "yacl/utils/parallel.h"

namespace psi::rr22::okvs {

namespace {

constexpr size_t kBatchSize = 8;

}  // namespace

void AesCrHash::Hash(absl::Span<const uint8_t> plaintext,
                     absl::Span<uint8_t> ciphertext) const {
  constexpr size_t block_size = yacl::crypto::SymmetricCrypto::BlockSize();
  constexpr size_t batch_byte_size = kBatchSize * block_size;

  std::vector<uint8_t> enc_outputs(batch_byte_size);

  size_t batch_max_index =
      (plaintext.size() / batch_byte_size) * batch_byte_size;

  size_t i = 0;
  for (i = 0; i < batch_max_index; i += batch_byte_size) {
    Encrypt(plaintext.subspan(i, batch_byte_size), absl::MakeSpan(enc_outputs));

    for (size_t j = 0; j < batch_byte_size; j++) {
      ciphertext[i + j] = enc_outputs[j] ^ plaintext[i + j];
    }
  }

  if (i < plaintext.size()) {
    Encrypt(plaintext.subspan(i, plaintext.size() - i),
            absl::MakeSpan(&enc_outputs[0], plaintext.size() - i));
    for (size_t j = 0; i < plaintext.size(); ++i, ++j) {
      ciphertext[i] = enc_outputs[j] ^ plaintext[i];
    }
  }
}

void AesCrHash::Hash(absl::Span<const uint128_t> plaintext,
                     absl::Span<uint128_t> ciphertext) const {
  size_t batch_max_index = plaintext.size() / kBatchSize * kBatchSize;

  std::vector<uint128_t> enc_outputs(kBatchSize);

  for (size_t i = 0; i < batch_max_index; i += kBatchSize) {
    Encrypt(plaintext.subspan(i, kBatchSize), absl::MakeSpan(enc_outputs));

    ciphertext[i + 0] = enc_outputs[0] ^ plaintext[i + 0];
    ciphertext[i + 1] = enc_outputs[1] ^ plaintext[i + 1];
    ciphertext[i + 2] = enc_outputs[2] ^ plaintext[i + 2];
    ciphertext[i + 3] = enc_outputs[3] ^ plaintext[i + 3];
    ciphertext[i + 4] = enc_outputs[4] ^ plaintext[i + 4];
    ciphertext[i + 5] = enc_outputs[5] ^ plaintext[i + 5];
    ciphertext[i + 6] = enc_outputs[6] ^ plaintext[i + 6];
    ciphertext[i + 7] = enc_outputs[7] ^ plaintext[i + 7];
  }

  if (batch_max_index < plaintext.size()) {
    Encrypt(
        plaintext.subspan(batch_max_index, plaintext.size() - batch_max_index),
        absl::MakeSpan(&enc_outputs[0], plaintext.size() - batch_max_index));

    size_t i = batch_max_index;

    for (size_t j = 0; i < plaintext.size(); ++i, ++j) {
      ciphertext[i] = enc_outputs[j] ^ plaintext[i];
    }
  }
}

uint128_t AesCrHash::Hash(uint128_t input) const {
  uint128_t output = Encrypt(input) ^ input;

  return output;
}

}  // namespace psi::rr22::okvs
