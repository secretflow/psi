#include "experimental/pir/piano/util.h"

namespace pir::piano {

uint128_t SecureRandKey() { return yacl::crypto::SecureRandU128(); }

uint64_t PRFEval(const uint128_t key, const uint64_t x) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(key, &aes_key);

  const auto src_block = static_cast<uint128_t>(x);
  std::vector<uint128_t> plain_blocks(1);
  plain_blocks[0] = src_block;
  std::vector<uint128_t> cipher_blocks(1);

  AES_ecb_encrypt_blks(aes_key, absl::MakeConstSpan(plain_blocks),
                       absl::MakeSpan(cipher_blocks));
  return static_cast<uint64_t>(cipher_blocks[0]);
}

// Generate ChunkSize and SetSize
std::pair<uint64_t, uint64_t> GenParams(const uint64_t entry_num) {
  const double targetChunkSize = 2 * std::sqrt(static_cast<double>(entry_num));
  uint64_t ChunkSize = 1;

  // Ensure ChunkSize is a power of 2 and not smaller than targetChunkSize
  while (ChunkSize < static_cast<uint64_t>(targetChunkSize)) {
    ChunkSize *= 2;
  }

  uint64_t SetSize = (entry_num + ChunkSize - 1) / ChunkSize;
  // Round up to the next multiple of 4
  SetSize = (SetSize + 3) / 4 * 4;

  return {ChunkSize, SetSize};
}

yacl::crypto::AES_KEY GetLongKey(const uint128_t key) {
  yacl::crypto::AES_KEY aes_key;
  AES_set_encrypt_key(key, &aes_key);
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

}  // namespace pir::piano
