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

#include "psi/algorithm/ecdh/ub_psi/ecdh_oprf_psi.h"
#include "psi/interface.h"
#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/join_processor.h"
#include "psi/utils/resource_manager.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::ecdh {

class EcdhUbPsiServer : public AbstractUbPsiServer {
 public:
  explicit EcdhUbPsiServer(const v2::UbPsiConfig& config,
                           std::shared_ptr<yacl::link::Context> lctx = nullptr);

  ~EcdhUbPsiServer() override = default;

  void Init() override;

  void OfflineGenCache() override;

  void OfflineTransferCache() override;

  void Online() override;

  void Offline() override;

 protected:
  struct IndexWithCnt {
    std::vector<uint32_t> index;
    std::vector<uint32_t> peer_dup_cnt;
  };

  IndexWithCnt TransCacheIndexesToRowIndexs(
      const std::unordered_map<uint32_t, uint32_t>& shuffle_index_cnt_map);

  std::shared_ptr<IBasicBatchProvider> GetInputCsvProvider();

  std::shared_ptr<EcdhOprfPsiServer> GetOprfServer(
      const std::vector<uint8_t>& private_key);

  std::shared_ptr<UbPsiCacheProvider> GetCacheProvider();

  EcdhOprfPsiOptions psi_options_;

  std::shared_ptr<DirResource> dir_resource_;
  std::shared_ptr<JoinProcessor> join_processor_;
};

}  // namespace psi::ecdh
