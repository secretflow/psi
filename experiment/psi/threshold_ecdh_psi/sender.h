// Copyright 2025
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

#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi_cache.h"

#include "psi/algorithm/ecdh/sender.h"

namespace psi::ecdh {

class ThresholdEcdhPsiSender : public EcdhPsiSender {
 public:
  explicit ThresholdEcdhPsiSender(
      const v2::PsiConfig& config,
      std::shared_ptr<yacl::link::Context> lctx = nullptr);

 private:
  void Online() override;

  void PostProcess() override;

  // Sender recv receiver's shuffle indexes, restore them, then send them to
  // receiver. If sender can touch the intersection, sender store the
  // intersection indexes by index_writer.
  void RestoreAndSendIndex(
      const std::shared_ptr<ThresholdEcdhPsiCacheProvider>& cache_provider,
      const std::shared_ptr<IndexWriter>& index_writer);

  // Sender send receiver's restored indexes to receiver.
  void SendRestoredIndex(const std::vector<uint32_t>& peer_restored_index);

  // Sender recv receiver's shuffled indexes and the count of unique keys in the
  // real intersection.
  std::pair<std::vector<uint32_t>, uint32_t> RecvShuffledIndexAndCount();

  // Sender recv its own intersection indexes.
  std::vector<uint32_t> RecvSelfIndex();

  uint32_t threshold_;

  std::shared_ptr<yacl::link::Context> index_link_ctx_;
};

}  // namespace psi::ecdh
