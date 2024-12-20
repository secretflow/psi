#include "experiment/pir/piano/util.h"

namespace pir::piano {

std::pair<uint64_t, uint64_t> GenChunkParams(const uint64_t entry_num) {
  const double target_chunk_size =
      2 * std::sqrt(static_cast<double>(entry_num));
  uint64_t chunk_size = 1;

  // Ensure chunk_size is a power of 2 and not smaller than target_chunk_size
  while (chunk_size < static_cast<uint64_t>(target_chunk_size)) {
    chunk_size *= 2;
  }

  uint64_t set_size = (entry_num + chunk_size - 1) / chunk_size;
  return {chunk_size, set_size};
}

uint128_t SecureRandKey() { return yacl::crypto::SecureRandU128(); }

yacl::crypto::AES_KEY GetLongKey(const uint128_t key) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(key, &aes_key);
  return aes_key;
}

uint64_t PRFEvalWithLongKeyAndTag(const yacl::crypto::AES_KEY& long_key,
                                  const uint32_t tag, const uint64_t x) {
  const uint128_t src_block = (static_cast<uint128_t>(tag) << 64) + x;
  std::vector<uint128_t> plain_blocks(1);
  plain_blocks[0] = src_block;
  std::vector<uint128_t> cipher_blocks(1);
  AES_ecb_encrypt_blks(long_key, absl::MakeConstSpan(plain_blocks),
                       absl::MakeSpan(cipher_blocks));
  return static_cast<uint64_t>(cipher_blocks[0]);
}

std::vector<uint64_t> PRFSetWithShortTag::ExpandWithLongKey(
    const yacl::crypto::AES_KEY& long_key, const uint64_t set_size,
    const uint64_t chunk_size) const {
  std::vector<uint64_t> expanded_set(set_size);
  for (uint64_t i = 0; i < set_size; i++) {
    const uint64_t tmp = PRFEvalWithLongKeyAndTag(long_key, tag, i);
    // Get the offset within the chunk
    const uint64_t offset = tmp & (chunk_size - 1);
    expanded_set[i] = i * chunk_size + offset;
  }
  return expanded_set;
}

bool PRFSetWithShortTag::MemberTestWithLongKey(
    const yacl::crypto::AES_KEY& long_key, const uint64_t chunk_id,
    const uint64_t offset, const uint64_t chunk_size) const {
  // Ensure chunk_size is a power of 2 and compare offsets
  return offset ==
         (PRFEvalWithLongKeyAndTag(long_key, tag, chunk_id) & (chunk_size - 1));
}

std::vector<uint8_t> FNVHash(uint64_t key) {
  const uint64_t fnv_offset_basis = 14695981039346656037ULL;
  uint64_t hash = fnv_offset_basis;

  for (int i = 0; i < 8; ++i) {
    const uint64_t fnv_prime = 1099511628211ULL;
    const auto byte = static_cast<uint8_t>(key & 0xFF);
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
