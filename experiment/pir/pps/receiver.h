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

#include <cstddef>
#include <set>

#include "ggm_pset.h"
#include "yacl/base/buffer.h"
#include "yacl/base/dynamic_bitset.h"
#include "yacl/link/context.h"

namespace pir::pps {
PIRKey Bytes_to_uint128(const std::array<std::byte, 16>& bytes);

void DeserializeOfflineMessage(const yacl::Buffer& buffer, PIRKey& sk,
                               std::set<uint64_t>& deltas);

void DeserializeOnlineMessage(const yacl::Buffer& buffer, PIRPuncKey& puncKey);

void DeserializeOfflineMessage(const yacl::Buffer& buffer,
                               std::vector<PIRKeyUnion>& pirKey);

void OfflineServerRecvFromClient(PIRKey& sk, std::set<uint64_t>& deltas,
                                 std::shared_ptr<yacl::link::Context> lctx);

void ClientRecvFromOfflineServer(yacl::dynamic_bitset<>& h,
                                 std::shared_ptr<yacl::link::Context> lctx);

void OnlineServerRecvFromClient(PIRPuncKey& puncKey,
                                std::shared_ptr<yacl::link::Context> lctx);

void ClientRecvFromOnlineServer(bool& a,
                                std::shared_ptr<yacl::link::Context> lctx);

void OfflineServerRecvFromClientM(std::vector<PIRKeyUnion>& pirKey,
                                  std::shared_ptr<yacl::link::Context> lctx);

}  // namespace pir::pps