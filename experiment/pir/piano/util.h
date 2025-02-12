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

#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "yacl/base/int128.h"
#include "yacl/crypto/aes/aes_intrinsics.h"
#include "yacl/crypto/rand/rand.h"

namespace pir::piano {

class DBEntry {
 public:
  DBEntry() = default;

  // Total byte size of the database entry, initializing all bytes to zero
  explicit DBEntry(size_t entry_size) : data_(entry_size, 0) {}
  explicit DBEntry(const std::vector<uint8_t>& data) : data_(data) {}

  // Accessor for the underlying data
  [[nodiscard]] const std::vector<uint8_t>& GetData() const { return data_; }

  // XOR operations
  void Xor(const DBEntry& other) {
    YACL_ENFORCE_EQ(data_.size(), other.data_.size());
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i] ^= other.data_[i];
    }
  }

  void XorFromRaw(absl::Span<const uint8_t> src) {
    YACL_ENFORCE_EQ(data_.size(), src.size());
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i] ^= src[i];
    }
  }

  // Static method to generate a zero-filled DBEntry
  static DBEntry ZeroEntry(size_t entry_size) { return DBEntry(entry_size); }

  // Generate a DBEntry based on a seed and id using a custom hash function
  static DBEntry GenDBEntry(
      size_t entry_size, uint64_t key, uint64_t id,
      const std::function<std::vector<uint8_t>(uint64_t)>& hash_func) {
    DBEntry entry(entry_size);
    const std::vector<uint8_t> hash = hash_func(key ^ id);
    for (size_t i = 0; i < entry_size; ++i) {
      if (i < hash.size()) {
        entry.data_[i] = hash[i];
      } else {
        entry.data_[i] = 0;
      }
    }
    return entry;
  }

  // Convert a slice (vector) into a DBEntry structure
  static DBEntry DBEntryFromSlice(const std::vector<uint8_t>& s) {
    return DBEntry(s);
  }

 private:
  std::vector<uint8_t> data_;
};

/**
 * @brief Generate optimal chunk and set size for PIR parameters.
 *
 * Calculate chunk size and set size based on total number of entries.
 * The chunk size is set to the smallest power of 2 that is >= 2*sqrt(n),
 * which optimizes modulo operations and overall scheme performance.
 *
 * @param entry_num Total number of entries in the database.
 * @return A pair of {chunk_size, set_size}.
 */
std::pair<uint64_t, uint64_t> GenChunkParams(uint64_t entry_num);

// Generate secure master key
uint128_t SecureRandKey();

// Return a long key (AES expanded key) for PRF evaluation
yacl::crypto::AES_KEY GetLongKey(uint128_t key);

/**
 * @brief Evaluate a Pseudo-Random Function (PRF) using AES-ECB encryption.
 *
 * Combine a 32-bit tag and 64-bit input into a 128-bit block, encrypt using a
 * long key, and return the lower 64 bits of the encrypted block.
 *
 * @param long_key AES encryption key.
 * @param tag 32-bit tag for domain separation.
 * @param x 64-bit input value.
 * @return 64-bit pseudo-random output.
 */
uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  uint32_t tag, uint64_t x);

struct PRFSetWithShortTag {
  uint32_t tag;

  // Expand a short-tag set to a full set using the PRFEval
  [[nodiscard]] std::vector<uint64_t> ExpandWithLongKey(
      const yacl::crypto::AES_KEY& long_key, uint64_t set_size,
      uint64_t chunk_size) const;

  // Check if an element belongs to the expanded set
  [[nodiscard]] bool MemberTestWithLongKey(
      const yacl::crypto::AES_KEY& long_key, uint64_t chunk_id, uint64_t offset,
      uint64_t chunk_size) const;
};

/**
 * @brief Convert a 64-bit key to an 8-byte hash using FNV-1a algorithm.
 *
 * Used for generating test data or random padding, independent of specific PIR
 * scheme.
 *
 * @param key Input 64-bit value to be hashed.
 * @return 8-byte hash representation.
 */
std::vector<uint8_t> FNVHash(uint64_t key);

}  // namespace pir::piano
