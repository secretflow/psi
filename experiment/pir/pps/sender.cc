#include <cstddef>
#include <set>

#include "ggm_pset.h"
#include "yacl/base/buffer.h"
#include "yacl/base/dynamic_bitset.h"
#include "yacl/link/context.h"

namespace pir::pps {
std::array<std::byte, 16> Uint128_to_bytes(PIRKey sk) {
  std::array<std::byte, 16> bytes;
  uint64_t high = static_cast<uint64_t>(sk >> 64);
  uint64_t low = static_cast<uint64_t>(sk & 0xFFFFFFFFFFFFFFFF);
  std::memcpy(bytes.data(), &high, sizeof(high));
  std::memcpy(bytes.data() + sizeof(high), &low, sizeof(low));
  return bytes;
}

yacl::Buffer SerializeOfflineMessage(PIRKey& sk, std::set<uint64_t>& deltas) {
  size_t total_size =
      sizeof(sk) + sizeof(size_t) + deltas.size() * sizeof(uint64_t);
  yacl::Buffer buffer(total_size);
  std::byte* data_ptr = buffer.data<std::byte>();
  auto sk_bytes = Uint128_to_bytes(sk);
  std::memcpy(data_ptr, sk_bytes.data(), sk_bytes.size());
  data_ptr += sk_bytes.size();
  size_t set_size = deltas.size();
  std::memcpy(data_ptr, &set_size, sizeof(set_size));
  data_ptr += sizeof(set_size);
  for (uint64_t delta : deltas) {
    std::memcpy(data_ptr, &delta, sizeof(delta));
    data_ptr += sizeof(delta);
  }
  return buffer;
}

yacl::Buffer SerializeOnlineMessage(PIRPuncKey& puncKey) {
  size_t total_size = sizeof(size_t) + puncKey.k_.size() * sizeof(uint128_t) +
                      sizeof(puncKey.pos_) + sizeof(puncKey.delta_);
  yacl::Buffer buffer(total_size);
  std::byte* data_ptr = buffer.data<std::byte>();
  size_t k_size = puncKey.k_.size();
  std::memcpy(data_ptr, &k_size, sizeof(size_t));
  data_ptr += sizeof(size_t);
  std::memcpy(data_ptr, puncKey.k_.data(), k_size * sizeof(uint128_t));
  data_ptr += k_size * sizeof(uint128_t);
  std::memcpy(data_ptr, &puncKey.pos_, sizeof(puncKey.pos_));
  data_ptr += sizeof(puncKey.pos_);
  std::memcpy(data_ptr, &puncKey.delta_, sizeof(puncKey.delta_));
  return buffer;
}

yacl::Buffer SerializeOfflineMessage(std::vector<PIRKeyUnion>& pirKey) {
  size_t total_size =
      sizeof(size_t) + pirKey.size() * (sizeof(uint128_t) + sizeof(uint64_t));
  yacl::Buffer buffer(total_size);
  std::byte* data_ptr = buffer.data<std::byte>();
  size_t size = pirKey.size();
  std::memcpy(data_ptr, &size, sizeof(size_t));
  data_ptr += sizeof(size_t);
  for (const auto& unionItem : pirKey) {
    std::memcpy(data_ptr, &unionItem.k_, sizeof(uint128_t));
    data_ptr += sizeof(uint128_t);
    std::memcpy(data_ptr, &unionItem.delta_, sizeof(uint64_t));
    data_ptr += sizeof(uint64_t);
  }
  return buffer;
}

void ClientSendToOfflineServer(PIRKey& sk, std::set<uint64_t>& deltas,
                               std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeOfflineMessage(sk, deltas);
  lctx->SendAsync(lctx->NextRank(), msg, "ClientSendToOfflineServerMsg");
}

void OfflineServerSendToClient(yacl::dynamic_bitset<>& h,
                               std::shared_ptr<yacl::link::Context> lctx) {
  lctx->SendAsync(lctx->NextRank(), h.to_string(), "OfflineServerSendToClient");
}

void ClientSendToOnlineServer(PIRPuncKey& puncKey,
                              std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeOnlineMessage(puncKey);
  lctx->SendAsync(lctx->NextRank(), msg, "ClientSendToOnlineServer");
}

void OnlineServerSendToClient(bool& a,
                              std::shared_ptr<yacl::link::Context> lctx) {
  std::string msg = a ? "1" : "0";
  lctx->SendAsync(lctx->NextRank(), msg, "OnlineServerSendToClient");
}

void ClientSendToOfflineServerM(std::vector<PIRKeyUnion>& pirKey,
                               std::shared_ptr<yacl::link::Context> lctx) {
  yacl::Buffer msg = SerializeOfflineMessage(pirKey);
  lctx->SendAsync(lctx->NextRank(), msg, "ClientSendToOfflineServerMsg");
}

}  // namespace pir::pps
