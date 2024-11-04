#pragma once

#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "yacl/crypto/aes/aes_intrinsics.h"

namespace pir::piano {

constexpr size_t DBEntrySize = 8;  // has to be a multiple of 8
constexpr size_t DBEntryLength = DBEntrySize / 8;

using PrfKey128 = uint128_t;
using DBEntry = std::array<uint64_t, DBEntryLength>;
using PrfKey = PrfKey128;

uint128_t BytesToUint128(const std::string& bytes);

std::string Uint128ToBytes(uint128_t value);

// Generates a random 128-bit key using the provided RNG
PrfKey128 RandKey128(std::mt19937_64& rng);

// Generates a random PRF key
PrfKey RandKey(std::mt19937_64& rng);

// Evaluates PRF using 128-bit key and returns a 64-bit result
uint64_t PRFEval128(const PrfKey128* key, uint64_t x);

// Evaluates PRF using a general PrfKey and returns a 64-bit result
uint64_t PRFEval(const PrfKey* key, uint64_t x);

// XOR two DBEntry structures
void DBEntryXor(DBEntry* dst, const DBEntry* src);

// XOR a DBEntry with raw uint64_t data
void DBEntryXorFromRaw(DBEntry* dst, const uint64_t* src);

// Compare two DBEntry structures for equality
bool EntryIsEqual(const DBEntry& a, const DBEntry& b);

// Generate a random DBEntry using the provided RNG
DBEntry RandDBEntry(std::mt19937_64& rng);

// Default FNV hash implementation for 64-bit keys
uint64_t DefaultHash(uint64_t key);

// Generate a DBEntry based on a key and ID
DBEntry GenDBEntry(uint64_t key, uint64_t id);

// Generate a zero-filled DBEntry
DBEntry ZeroEntry();

// Convert a slice (array) into a DBEntry structure
DBEntry DBEntryFromSlice(const std::array<uint64_t, DBEntryLength>& s);

// Generate parameters for ChunkSize and SetSize
std::pair<uint64_t, uint64_t> GenParams(uint64_t db_size);

// Returns a long key (AES expanded key) for PRF evaluation
yacl::crypto::AES_KEY GetLongKey(const PrfKey128* key);

// PRF evaluation with a long key and tag, returns a 64-bit result
uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  uint32_t tag, uint64_t x);

class PRSetWithShortTag {
 public:
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
