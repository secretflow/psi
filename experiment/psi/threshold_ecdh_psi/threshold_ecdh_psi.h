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

#include "psi/algorithm/ecdh/ecdh_psi.h"

namespace psi::ecdh {

class ThresholdEcdhPsiCtx : public EcdhPsiContext {
 public:
  explicit ThresholdEcdhPsiCtx(const EcdhPsiOptions& options);
  ~ThresholdEcdhPsiCtx() = default;

  // Sender recv receiver's masked items, mask them again then shuffle and send
  // them back. The receiver's indexes, shuffled indexes and duplicate count is
  // stored in `ThresholdEcdhPsiCache`. The implementation refers to
  // `EcdhPsiContext::MaskPeer`.
  void MaskAndShufflePeer(const std::shared_ptr<ThresholdEcdhPsiCache>& cache);

  // Receiver recv sender's masked items, mask them again. In this case,
  // receiver will not send them back. Sender's dual masked items will be used
  // by receiver to get the intersection. The implementation refers to
  // `EcdhPsiContext::MaskPeer`.
  void RecvAndMaskPeer(
      const std::shared_ptr<IEcPointStore>& peer_ec_point_store);
};

void RunSender(const EcdhPsiOptions& options,
               const std::filesystem::path& cache_path,
               const std::shared_ptr<IBasicBatchProvider>& batch_provider,
               const std::shared_ptr<IEcPointStore>& self_ec_point_store,
               const std::shared_ptr<IEcPointStore>& peer_ec_point_store);

void RunReceiver(const EcdhPsiOptions& options,
                 const std::shared_ptr<IBasicBatchProvider>& batch_provider,
                 const std::shared_ptr<IEcPointStore>& self_ec_point_store,
                 const std::shared_ptr<IEcPointStore>& peer_ec_point_store);
}  // namespace psi::ecdh