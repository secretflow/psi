#include <cstddef>
#include <set>

#include "ggm_pset.h"
#include "yacl/base/buffer.h"
#include "yacl/base/dynamic_bitset.h"
#include "yacl/link/context.h"

namespace pir::pps {
std::array<std::byte, 16> Uint128_to_bytes(PIRKey sk);

yacl::Buffer SerializeOfflineMessage(PIRKey& sk, std::set<uint64_t>& deltas);

yacl::Buffer SerializeOnlineMessage(PIRPuncKey& puncKey);

yacl::Buffer SerializeOfflineMessage(std::vector<PIRKeyUnion>& pirKey);

void ClientSendToOfflineServer(PIRKey& sk, std::set<uint64_t>& deltas,
                               std::shared_ptr<yacl::link::Context> lctx);

void OfflineServerSendToClient(yacl::dynamic_bitset<>& h,
                               std::shared_ptr<yacl::link::Context> lctx);

void ClientSendToOnlineServer(PIRPuncKey& puncKey,
                              std::shared_ptr<yacl::link::Context> lctx);

void OnlineServerSendToClient(bool& a,
                              std::shared_ptr<yacl::link::Context> lctx);

void ClientSendToOfflineServerM(std::vector<PIRKeyUnion>& pirKey,
                                std::shared_ptr<yacl::link::Context> lctx);
}  // namespace pir::pps