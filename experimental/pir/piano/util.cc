#include "experimental/pir/piano/util.h"

namespace pir::piano {

uint128_t BytesToUint128(const std::string& bytes) {
  if (bytes.size() != 16) {
    SPDLOG_WARN("Bytes size must be 16 for uint128_t conversion.");
  }

  uint128_t result = 0;
  std::memcpy(&result, bytes.data(), 16);
  return result;
}

std::string Uint128ToBytes(const uint128_t value) {
  std::string bytes(16, 0);
  std::memcpy(bytes.data(), &value, 16);
  return bytes;
}

PrfKey128 RandKey128(std::mt19937_64& rng) {
  const uint64_t lo = rng();
  const uint64_t hi = rng();
  return yacl::MakeUint128(hi, lo);
}

PrfKey RandKey(std::mt19937_64& rng) { return RandKey128(rng); }

uint64_t PRFEval128(const PrfKey128* key, const uint64_t x) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(*key, &aes_key);

  const auto src_block = static_cast<uint128_t>(x);
  std::vector<uint128_t> plain_blocks(1);
  plain_blocks[0] = src_block;
  std::vector<uint128_t> cipher_blocks(1);

  AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(plain_blocks),
                       absl::MakeSpan(cipher_blocks));
  return static_cast<uint64_t>(cipher_blocks[0]);
}

uint64_t PRFEval(const PrfKey* key, const uint64_t x) {
  return PRFEval128(key, x);
}

void DBEntryXor(DBEntry* dst, const DBEntry* src) {
  for (size_t i = 0; i < DBEntryLength; ++i) {
    (*dst)[i] ^= (*src)[i];
  }
}

void DBEntryXorFromRaw(DBEntry* dst, const uint64_t* src) {
  for (size_t i = 0; i < DBEntryLength; ++i) {
    (*dst)[i] ^= src[i];
  }
}

bool EntryIsEqual(const DBEntry& a, const DBEntry& b) {
  for (size_t i = 0; i < DBEntryLength; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

DBEntry RandDBEntry(std::mt19937_64& rng) {
  DBEntry entry;
  for (size_t i = 0; i < DBEntryLength; ++i) {
    entry[i] = rng();
  }
  return entry;
}

uint64_t DefaultHash(uint64_t key) {
  constexpr uint64_t FNV_offset_basis = 14695981039346656037ULL;
  uint64_t hash = FNV_offset_basis;
  for (int i = 0; i < 8; ++i) {
    constexpr uint64_t FNV_prime = 1099511628211ULL;
    const auto byte = static_cast<uint8_t>(key & 0xFF);
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV_prime;
    key >>= 8;
  }
  return hash;
}

DBEntry GenDBEntry(const uint64_t key, const uint64_t id) {
  DBEntry entry;
  for (size_t i = 0; i < DBEntryLength; ++i) {
    entry[i] = DefaultHash((key ^ id) + i);
  }
  return entry;
}

DBEntry ZeroEntry() {
  DBEntry entry = {};
  for (size_t i = 0; i < DBEntryLength; ++i) {
    entry[i] = 0;
  }
  return entry;
}

DBEntry DBEntryFromSlice(const std::array<uint64_t, DBEntryLength>& s) {
  DBEntry entry;
  for (size_t i = 0; i < DBEntryLength; ++i) {
    entry[i] = s[i];
  }
  return entry;
}

// Generate ChunkSize and SetSize
std::pair<uint64_t, uint64_t> GenParams(const uint64_t db_size) {
  const double targetChunkSize = 2 * std::sqrt(static_cast<double>(db_size));
  uint64_t ChunkSize = 1;

  // Ensure ChunkSize is a power of 2 and not smaller than targetChunkSize
  while (ChunkSize < static_cast<uint64_t>(targetChunkSize)) {
    ChunkSize *= 2;
  }

  uint64_t SetSize = (db_size + ChunkSize - 1) / ChunkSize;
  // Round up to the next multiple of 4
  SetSize = (SetSize + 3) / 4 * 4;

  return {ChunkSize, SetSize};
}

yacl::crypto::AES_KEY GetLongKey(const PrfKey128* key) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(*key, &aes_key);
  return aes_key;
}

uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  const uint32_t tag, const uint64_t x) {
  // Combine tag and x into a 128-bit block by shifting tag to the high 64 bits
  const uint128_t src_block = (static_cast<uint128_t>(tag) << 64) + x;
  std::vector<uint128_t> plain_blocks(1);
  plain_blocks[0] = src_block;
  std::vector<uint128_t> cipher_blocks(1);
  AES_ecb_encrypt_blks(long_key, absl::MakeConstSpan(plain_blocks),
                       absl::MakeSpan(cipher_blocks));
  return static_cast<uint64_t>(cipher_blocks[0]);
}

std::vector<uint64_t> PRSetWithShortTag::ExpandWithLongKey(
    const yacl::crypto::AES_KEY& long_key, const uint64_t set_size,
    const uint64_t chunk_size) const {
  std::vector<uint64_t> expandedSet(set_size);
  for (uint64_t i = 0; i < set_size; i++) {
    const uint64_t tmp = PRFEvalWithLongKeyAndTag(long_key, Tag, i);
    // Get the offset within the chunk
    const uint64_t offset = tmp & (chunk_size - 1);
    expandedSet[i] = i * chunk_size + offset;
  }
  return expandedSet;
}

bool PRSetWithShortTag::MemberTestWithLongKeyAndTag(
    const yacl::crypto::AES_KEY& long_key, const uint64_t chunk_id,
    const uint64_t offset, const uint64_t chunk_size) const {
  // Ensure chunk_size is a power of 2 and compare offsets
  return offset ==
         (PRFEvalWithLongKeyAndTag(long_key, Tag, chunk_id) & (chunk_size - 1));
}

}
