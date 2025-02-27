// Copyright 2024 The secretflow authors.
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

#include "experiment/pir/piano/util.h"

namespace pir::piano {

std::pair<uint64_t, uint64_t> GenChunkParams(uint64_t entry_num) {
  double target_chunk_size = 2 * std::sqrt(static_cast<double>(entry_num));
  uint64_t chunk_size = 1;

  // Ensure chunk_size is a power of 2 and not smaller than target_chunk_size
  while (static_cast<double>(chunk_size) < target_chunk_size) {
    chunk_size *= 2;
  }

  uint64_t set_size = (entry_num + chunk_size - 1) / chunk_size;
  return {chunk_size, set_size};
}

uint128_t SecureRandKey() { return yacl::crypto::SecureRandU128(); }

yacl::crypto::AES_KEY GetLongKey(uint128_t key) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(key, &aes_key);
  return aes_key;
}

uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  uint32_t tag, uint64_t x) {
  uint128_t src_block = (static_cast<uint128_t>(tag) << 64) + x;
  std::vector<uint128_t> plain_blocks(1);
  plain_blocks[0] = src_block;
  std::vector<uint128_t> cipher_blocks(1);
  AES_ecb_encrypt_blks(long_key, absl::MakeConstSpan(plain_blocks),
                       absl::MakeSpan(cipher_blocks));
  return static_cast<uint64_t>(cipher_blocks[0]);
}

std::vector<uint64_t> PRFSetWithShortTag::ExpandWithLongKey(
    const yacl::crypto::AES_KEY& long_key, uint64_t set_size,
    uint64_t chunk_size) const {
  std::vector<uint64_t> expanded_set(set_size);
  for (uint64_t i = 0; i < set_size; i++) {
    uint64_t tmp = PRFEvalWithLongKeyAndTag(long_key, tag, i);
    // Get the offset within the chunk
    uint64_t offset = tmp & (chunk_size - 1);
    expanded_set[i] = i * chunk_size + offset;
  }
  return expanded_set;
}

bool PRFSetWithShortTag::MemberTestWithLongKey(
    const yacl::crypto::AES_KEY& long_key, uint64_t chunk_id, uint64_t offset,
    uint64_t chunk_size) const {
  // Ensure chunk_size is a power of 2 and compare offsets
  return offset ==
         (PRFEvalWithLongKeyAndTag(long_key, tag, chunk_id) & (chunk_size - 1));
}

std::vector<uint8_t> FNVHash(uint64_t key) {
  const uint64_t fnv_offset_basis = 14695981039346656037ULL;
  uint64_t hash = fnv_offset_basis;

  for (int i = 0; i < 8; ++i) {
    const uint64_t fnv_prime = 1099511628211ULL;
    auto byte = static_cast<uint8_t>(key & 0xFF);
    hash ^= static_cast<uint64_t>(byte);
    hash *= fnv_prime;
    key >>= 8;
  }

  std::vector<uint8_t> hash_bytes(8);
  for (size_t i = 0; i < 8; ++i) {
    hash_bytes[i] = static_cast<uint8_t>((hash >> (i * 8)) & 0xFF);
  }
  return hash_bytes;
}

}  // namespace pir::piano
