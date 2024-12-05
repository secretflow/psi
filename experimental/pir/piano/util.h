#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/crypto/aes/aes_intrinsics.h"
#include "yacl/crypto/rand/rand.h"

namespace pir::piano {

class DBEntry {
 public:
  DBEntry() = default;

  // entry_size represents the number of bytes in the DBEntry
  explicit DBEntry(const size_t entry_size)
      : k_length_(entry_size), data_(entry_size, 0) {}

  explicit DBEntry(const std::vector<uint8_t>& data)
      : k_length_(data.size()), data_(data) {}

  // Accessor for the underlying data
  std::vector<uint8_t>& data() { return data_; }
  [[nodiscard]] const std::vector<uint8_t>& data() const { return data_; }

  // XOR operations
  void Xor(const DBEntry& other) {
    for (size_t i = 0; i < k_length_; ++i) {
      data_[i] ^= other.data_[i];
    }
  }

  void XorFromRaw(const uint8_t* src) {
    for (size_t i = 0; i < k_length_; ++i) {
      data_[i] ^= src[i];
    }
  }

  // Static method to generate a zero-filled DBEntry
  static DBEntry ZeroEntry(const size_t entry_size) {
    return DBEntry(entry_size);
  }

  // Generate a DBEntry based on a key and ID using a custom hash function
  static DBEntry GenDBEntry(
      const size_t entry_size, const uint64_t key, const uint64_t id,
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
  size_t k_length_{};
  std::vector<uint8_t> data_;
};

// Generate secure master key
uint128_t SecureRandKey();

// Evaluates PRF using a 128-bit key and returns a 64-bit result
uint64_t PRFEval(uint128_t key, uint64_t x);

// Generate parameters for ChunkSize and SetSize
std::pair<uint64_t, uint64_t> GenParams(uint64_t entry_num);

// Returns a long key (AES expanded key) for PRF evaluation
yacl::crypto::AES_KEY GetLongKey(uint128_t key);

// PRF evaluation with a long key and tag, returns a 64-bit result
uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  uint32_t tag, uint64_t x);

struct PRSetWithShortTag {
  uint32_t Tag;

  // Expands the set with a long key and tag
  [[nodiscard]] std::vector<uint64_t> ExpandWithLongKey(
      const yacl::crypto::AES_KEY& long_key, uint64_t set_size,
      uint64_t chunk_size) const;

  // Membership test with a long key and tag, to check if an ID belongs to the
  // set
  [[nodiscard]] bool MemberTestWithLongKeyAndTag(
      const yacl::crypto::AES_KEY& long_key, uint64_t chunk_id, uint64_t offset,
      uint64_t chunk_size) const;
};

}  // namespace pir::piano
