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

#include "psi/ecdh/server.h"

#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/ec.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {

EcdhUbPsiServer::EcdhUbPsiServer(const v2::UbPsiConfig &config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiServer(config, std::move(lctx)) {}

void EcdhUbPsiServer::Init() {
  YACL_ENFORCE(config_.mode() != v2::UbPsiConfig::MODE_UNSPECIFIED);

  if (config_.mode() == v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE ||
      config_.mode() == v2::UbPsiConfig::MODE_OFFLINE ||
      config_.mode() == v2::UbPsiConfig::MODE_FULL) {
    YACL_ENFORCE(!config_.input_config().path().empty());
    YACL_ENFORCE(!config_.keys().empty());
    YACL_ENFORCE(!config_.server_secret_key_path().empty());
    YACL_ENFORCE(!config_.cache_path().empty());
  }

  if (config_.mode() == v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE) {
    return;
  }

  if (config_.mode() == v2::UbPsiConfig::MODE_OFFLINE_TRANSFER_CACHE ||
      config_.mode() == v2::UbPsiConfig::MODE_OFFLINE ||
      config_.mode() == v2::UbPsiConfig::MODE_FULL) {
    YACL_ENFORCE(!config_.cache_path().empty());
  }

  if (config_.mode() == v2::UbPsiConfig::MODE_ONLINE ||
      config_.mode() == v2::UbPsiConfig::MODE_FULL) {
    YACL_ENFORCE(!config_.server_secret_key_path().empty());
    YACL_ENFORCE(!config_.cache_path().empty());
  }

  if (lctx_) {
    // Test connection.
    lctx_->ConnectToMesh();
    psi_options_.link0 = lctx_;
    psi_options_.link1 = lctx_->Spawn();
  }
}

void EcdhUbPsiServer::OfflineGenCache() {
  std::vector<uint8_t> server_private_key =
      ReadEcSecretKeyFile(config_.server_secret_key_path());

  std::shared_ptr<EcdhOprfPsiServer> dh_oprf_psi_server =
      std::make_shared<EcdhOprfPsiServer>(psi_options_, server_private_key);

  std::vector<std::string> selected_keys(config_.keys().begin(),
                                         config_.keys().end());

  std::shared_ptr<ArrowCsvBatchProvider> csv_batch_provider =
      std::make_shared<ArrowCsvBatchProvider>(config_.input_config().path(),
                                              selected_keys);

  std::shared_ptr<IShuffledBatchProvider> shuffle_batch_provider =
      std::make_shared<SimpleShuffledBatchProvider>(
          csv_batch_provider, psi_options_.batch_size, true);

  std::shared_ptr<IUbPsiCache> ub_cache = std::make_shared<UbPsiCache>(
      config_.cache_path(), dh_oprf_psi_server->GetCompareLength(),
      selected_keys);

  size_t self_items_count =
      dh_oprf_psi_server->FullEvaluate(shuffle_batch_provider, ub_cache);

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(-1);
}

void EcdhUbPsiServer::OfflineTransferCache() {
  std::shared_ptr<EcdhOprfPsiServer> ub_psi_server_transfer_cache =
      std::make_shared<EcdhOprfPsiServer>(psi_options_);

  std::shared_ptr<IBasicBatchProvider> batch_provider =
      std::make_shared<UbPsiCacheProvider>(
          config_.cache_path(), psi_options_.batch_size,
          ub_psi_server_transfer_cache->GetCompareLength());

  size_t self_items_count =
      ub_psi_server_transfer_cache->SendFinalEvaluatedItems(batch_provider);

  yacl::link::Barrier(lctx_, "ubpsi_offline_transfer_cache");

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(-1);
}

void EcdhUbPsiServer::Offline() {
  std::vector<uint8_t> server_private_key =
      ReadEcSecretKeyFile(config_.server_secret_key_path());

  std::shared_ptr<EcdhOprfPsiServer> dh_oprf_psi_server_offline =
      std::make_shared<EcdhOprfPsiServer>(psi_options_, server_private_key);

  std::vector<std::string> selected_keys(config_.keys().begin(),
                                         config_.keys().end());

  std::shared_ptr<ArrowCsvBatchProvider> csv_batch_provider =
      std::make_shared<ArrowCsvBatchProvider>(config_.input_config().path(),
                                              selected_keys);

  std::shared_ptr<IShuffledBatchProvider> shuffle_batch_provider =
      std::make_shared<SimpleShuffledBatchProvider>(
          csv_batch_provider, psi_options_.batch_size, true);

  size_t self_items_count =
      dh_oprf_psi_server_offline->FullEvaluateAndSend(shuffle_batch_provider);

  yacl::link::Barrier(lctx_, "ubpsi_offline_transfer_cache");

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(-1);
}

void EcdhUbPsiServer::Online() {
  std::vector<uint8_t> server_private_key =
      ReadEcSecretKeyFile(config_.server_secret_key_path());

  std::shared_ptr<EcdhOprfPsiServer> dh_oprf_psi_server_online =
      std::make_shared<EcdhOprfPsiServer>(psi_options_, server_private_key);

  dh_oprf_psi_server_online->RecvBlindAndSendEvaluate();

  yacl::link::Barrier(lctx_, "ubpsi_online");

  report_.set_intersection_count(-1);
}

}  // namespace psi::ecdh
