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

#include "psi/algorithm/ecdh/receiver.h"

namespace psi::ecdh {

class ThresholdEcdhPsiReceiver : public EcdhPsiReceiver {
 public:
  explicit ThresholdEcdhPsiReceiver(
      const v2::PsiConfig& config,
      std::shared_ptr<yacl::link::Context> lctx = nullptr);

 private:
  void Online() override;

  void PostProcess() override;

  // Receiver compute the shuffle indexes of intersection items, select no more
  // than t indexes, and send them to sender, where t is the intersection
  // threshold. After receiving the original indexes, receiver save them. If
  // sender can touch the intersection, receiver will send no more than the
  // threshold of sender's intersection indexes to sender.
  void ComputeAndStoreIndex(const std::shared_ptr<HashBucketEcPointStore>& self,
                            const std::shared_ptr<HashBucketEcPointStore>& peer,
                            const std::shared_ptr<IndexWriter>& index_writer);

  // Receiver send no more than the threshold of shuflled indexes and the count
  // of unique keys in the real intersection to sender.
  void SendShuffledIndexAndCount(
      const std::vector<uint32_t>& self_shuffled_index,
      uint32_t real_intersection_unique_key_count);

  // Receiver send no more than the threshold of sender's intersection indexes
  // to sender.
  void SendPeerIndex(const std::vector<uint32_t>& peer_index);

  // Receiver recv restored indexes.
  std::vector<uint32_t> RecvRestoredIndex();

  uint32_t threshold_;

  std::shared_ptr<yacl::link::Context> index_link_ctx_;
};
}  // namespace psi::ecdh