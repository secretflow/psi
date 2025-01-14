// Copyright 2024 Ant Group Co., Ltd.
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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/link/link.h"

#include "psi/algorithm/nty/server_aided_psi.h"

namespace psi::nty {

// Simple, Fast Malicious Multiparty Private Set Intersection
// https://eprint.iacr.org/2021/1221.pdf

struct NtyMPsiOptions {
  NtyMPsiOptions(size_t num_threads_params, size_t aided_server_rank_params,
                 size_t helper_rank_params, size_t receiver_rank_params,
                 bool broadcast_result_params = true,
                 bool malicious_params = false)
      : num_threads(num_threads_params),
        aided_server_rank(aided_server_rank_params),
        helper_rank(helper_rank_params),
        receiver_rank(receiver_rank_params),
        broadcast_result(broadcast_result_params),
        malicious(malicious_params) {}
  // number of threads
  size_t num_threads = 1;
  // aided_server is p_1 in the original paper, who generates prf key and ovks
  // encode reuslt p, and acts as the server to aid 2psi
  size_t aided_server_rank = 0;
  // helper is p_{n-1}
  size_t helper_rank = 1;
  // receiver is p_n
  size_t receiver_rank = 2;
  // broadcast intersection result to all
  bool broadcast_result = true;
  // run the protocol with malicious security
  // not supported by now
  bool malicious = false;
  std::shared_ptr<yacl::link::Context> link_ctx;
};

class NtyMPsi {
 public:
  explicit NtyMPsi(const NtyMPsiOptions& options);

  std::vector<uint128_t> Run(const std::vector<uint128_t>& inputs);

 private:
  void MultiPartyPsi(const std::vector<uint128_t>& inputs,
                     std::vector<uint128_t>& outputs);

  void ThreePartyPsi(const std::vector<uint128_t>& inputs,
                     std::vector<uint128_t>& outputs);

  void GenOkvs(const std::vector<uint128_t>& items,
               std::vector<uint128_t>& hashed_items, const size_t& okvs_size,
               const std::shared_ptr<yacl::link::Context>& ctx);

  void DecodeOkvs(const std::vector<uint128_t>& items, const size_t& okvs_size,
                  const std::shared_ptr<yacl::link::Context>& ctx,
                  std::vector<uint128_t>& recv_hashed_items);

  void GetIntersection(const std::vector<uint128_t>& inputs,
                       const std::vector<uint128_t>& hashed_items,
                       const std::vector<uint128_t>& raw_result,
                       std::vector<uint128_t>& intersection);

  // (ctx, world_size, self_rank)
  auto CollectContext() const {
    return std::make_tuple(options_.link_ctx, options_.link_ctx->WorldSize(),
                           options_.link_ctx->Rank());
  }

  NtyMPsiOptions options_;
  std::vector<std::shared_ptr<yacl::link::Context>> p2p_;
  uint64_t bin_size_ = 1 << 14;
  uint64_t paxos_weight_ = 3;
};

}  // namespace psi::nty
