// Copyright 2024 zhangwfjh
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

#include <memory>
#include <string>
#include <vector>

#include "yacl/base/int128.h"
#include "yacl/link/link.h"

namespace psi::psi {

// Practical Multi-party Private Set Intersection from Symmetric-Key Techniques
// https://eprint.iacr.org/2017/799.pdf

class KmprtParty {
 public:
  struct Options {
    std::shared_ptr<yacl::link::Context> link_ctx;
    size_t leader_rank;
  };

  KmprtParty(const Options& options);
  virtual std::vector<std::string> Run(const std::vector<std::string>& inputs);

 private:
  using Share = std::vector<uint64_t>;

  std::vector<uint128_t> EncodeInputs(const std::vector<std::string>& inputs,
                                      size_t count) const;
  std::vector<Share> ZeroSharing(size_t count) const;
  Share SwapShares(const std::vector<uint128_t>& items,
                   const std::vector<Share>& shares) const;
  Share Reconstruct(const std::vector<uint128_t>& items,
                    const Share& share) const;

  // (ctx, world_size, my_rank, leader_rank)
  auto CollectContext() const {
    return std::make_tuple(options_.link_ctx, options_.link_ctx->WorldSize(),
                           options_.link_ctx->Rank(), options_.leader_rank);
  }

  Options options_;
  std::vector<std::shared_ptr<yacl::link::Context>> p2p_;
};

}  // namespace psi::psi
