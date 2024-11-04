#include <cstddef>
#include <set>

#include "ggm_pset.h"
#include "yacl/base/buffer.h"
#include "yacl/base/dynamic_bitset.h"
#include "yacl/link/context.h"

namespace pir::pps {
PIRKey Bytes_to_uint128(const std::array<std::byte, 16>& bytes) {
  uint64_t high;
  uint64_t low;
  std::memcpy(&high, bytes.data(), sizeof(high));
  std::memcpy(&low, bytes.data() + sizeof(high), sizeof(low));
  return (static_cast<uint128_t>(high) << 64) | low;
}

void DeserializeOfflineMessage(const yacl::Buffer& buffer, PIRKey& sk,
                               std::set<uint64_t>& deltas) {
  std::array<std::byte, 16> sk_bytes{};
  const std::byte* data_ptr = buffer.data<std::byte>();
  std::memcpy(sk_bytes.data(), data_ptr, sk_bytes.size());
  sk = Bytes_to_uint128(sk_bytes);
  data_ptr += sk_bytes.size();
  size_t set_size;
  std::memcpy(&set_size, data_ptr, sizeof(set_size));
  data_ptr += sizeof(set_size);
  deltas.clear();
  for (size_t i = 0; i < set_size; ++i) {
    uint64_t delta;
    std::memcpy(&delta, data_ptr, sizeof(delta));
    deltas.insert(delta);
    data_ptr += sizeof(delta);
  }
}

void DeserializeOnlineMessage(const yacl::Buffer& buffer, PIRPuncKey& puncKey) {
  const std::byte* data_ptr = buffer.data<const std::byte>();
  size_t k_size;
  std::memcpy(&k_size, data_ptr, sizeof(size_t));
  data_ptr += sizeof(size_t);
  puncKey.k_.resize(k_size);
  std::memcpy(puncKey.k_.data(), data_ptr, k_size * sizeof(uint128_t));
  data_ptr += k_size * sizeof(uint128_t);
  std::memcpy(&puncKey.pos_, data_ptr, sizeof(puncKey.pos_));
  data_ptr += sizeof(puncKey.pos_);
  std::memcpy(&puncKey.delta_, data_ptr, sizeof(puncKey.delta_));
}

void DeserializeOfflineMessage(const yacl::Buffer& buffer,
                               std::vector<PIRKeyUnion>& pirKey) {
  const std::byte* data_ptr = buffer.data<const std::byte>();
  size_t size;
  std::memcpy(&size, data_ptr, sizeof(size_t));
  data_ptr += sizeof(size_t);
  pirKey.resize(size);
  for (size_t i = 0; i < size; ++i) {
    uint128_t k_val;
    std::memcpy(&k_val, data_ptr, sizeof(uint128_t));
    data_ptr += sizeof(uint128_t);
    uint64_t delta;
    std::memcpy(&delta, data_ptr, sizeof(uint64_t));
    data_ptr += sizeof(uint64_t);
    pirKey[i] = PIRKeyUnion(k_val, delta);
  }
}

void OfflineServerRecvFromClient(PIRKey& sk, std::set<uint64_t>& deltas,
                                 std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg =
      lctx->Recv(lctx->NextRank(), "OfflineServerRecvFromClient");
  DeserializeOfflineMessage(msg, sk, deltas);
}

void ClientRecvFromOfflineServer(yacl::dynamic_bitset<>& h,
                                 std::shared_ptr<yacl::link::Context> lctx) {
  std::string msg(std::string_view(
      lctx->Recv(lctx->NextRank(), "ClientRecvFromOfflineServer")));
  h = yacl::dynamic_bitset<>(msg);
}

void OnlineServerRecvFromClient(PIRPuncKey& puncKey,
                                std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = lctx->Recv(lctx->NextRank(), "OnlineServerRecvFromClient");
  DeserializeOnlineMessage(msg, puncKey);
}

void ClientRecvFromOnlineServer(bool& a,
                                std::shared_ptr<yacl::link::Context> lctx) {
  std::string msg(std::string_view(
      lctx->Recv(lctx->NextRank(), "ClientRecvFromOnlineServe")));
  a = (msg == "1");
}

void OfflineServerRecvFromClientM(std::vector<PIRKeyUnion>& pirKey,
                                  std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg =
      lctx->Recv(lctx->NextRank(), "OfflineServerRecvFromClient");
  DeserializeOfflineMessage(msg, pirKey);
}
}  // namespace pir::pps
