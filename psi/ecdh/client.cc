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

#include "psi/ecdh/client.h"

#include <filesystem>

#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"

#include "psi/legacy/bucket_psi.h"
#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {

EcdhUbPsiClient::EcdhUbPsiClient(const v2::UbPsiConfig& config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiClient(config, std::move(lctx)) {}

void EcdhUbPsiClient::Init() {
  YACL_ENFORCE(config_.mode() != v2::UbPsiConfig::MODE_UNSPECIFIED &&
               config_.mode() != v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE);

  if (config_.mode() == v2::UbPsiConfig::MODE_OFFLINE_TRANSFER_CACHE ||
      config_.mode() == v2::UbPsiConfig::MODE_OFFLINE ||
      config_.mode() == v2::UbPsiConfig::MODE_FULL) {
    YACL_ENFORCE(!config_.cache_path().empty());
  }

  if (config_.mode() == v2::UbPsiConfig::MODE_ONLINE ||
      config_.mode() == v2::UbPsiConfig::MODE_FULL) {
    YACL_ENFORCE(!config_.cache_path().empty());
  }

  if (lctx_) {
    // Test connection.
    lctx_->ConnectToMesh();

    psi_options_.link0 = lctx_;
    psi_options_.link1 = lctx_->Spawn();
  }
}

void EcdhUbPsiClient::OfflineGenCache() { YACL_THROW("unsupported."); }

void EcdhUbPsiClient::OfflineTransferCache() {
  std::shared_ptr<EcdhOprfPsiClient> ub_psi_client_transfer_cache =
      std::make_shared<EcdhOprfPsiClient>(psi_options_);

  auto peer_ec_point_store = std::make_shared<CachedCsvEcPointStore>(
      config_.cache_path(), false, "peer", false);

  ub_psi_client_transfer_cache->RecvFinalEvaluatedItems(peer_ec_point_store);

  peer_ec_point_store->Flush();

  yacl::link::Barrier(lctx_, "ubpsi_offline_transfer_cache");

  report_.set_original_count(peer_ec_point_store->ItemCount());
  report_.set_intersection_count(-1);
}

void EcdhUbPsiClient::Offline() { OfflineTransferCache(); }

void EcdhUbPsiClient::Online() {
  std::shared_ptr<EcdhOprfPsiClient> dh_oprf_psi_client_online =
      std::make_shared<EcdhOprfPsiClient>(psi_options_);

  std::vector<std::string> selected_keys(config_.keys().begin(),
                                         config_.keys().end());

  std::shared_ptr<IBasicBatchProvider> batch_provider =
      std::make_shared<ArrowCsvBatchProvider>(config_.input_config().path(),
                                              selected_keys);

  boost::uuids::random_generator uuid_generator;
  std::string ec_point_store_path1 =
      fmt::format("{}/{}/tmp-self-cipher-store-{}.csv",
                  std::filesystem::temp_directory_path().string(),
                  boost::uuids::to_string(uuid_generator()), lctx_->Rank());

  auto self_ec_point_store = std::make_shared<CachedCsvEcPointStore>(
      ec_point_store_path1, true, "self", false);

  auto peer_ec_point_store = std::make_shared<CachedCsvEcPointStore>(
      config_.cache_path(), false, "peer", true);

  SPDLOG_INFO("online protocol CachedCsvCipherStore: {} {}",
              ec_point_store_path1, config_.cache_path());

  std::future<size_t> f_client_send_blind = std::async([&] {
    return dh_oprf_psi_client_online->SendBlindedItems(batch_provider);
  });

  dh_oprf_psi_client_online->RecvEvaluatedItems(self_ec_point_store);

  self_ec_point_store->Flush();

  size_t self_items_count = f_client_send_blind.get();

  yacl::link::Barrier(lctx_, "ubpsi_online");

  std::vector<uint64_t> results;
  std::vector<std::string> masked_items;
  std::tie(results, masked_items) = FinalizeAndComputeIndices(
      self_ec_point_store, peer_ec_point_store, psi_options_.batch_size);

  YACL_ENFORCE(results.size() == masked_items.size());
  SPDLOG_INFO("indices size:{}", results.size());

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(results.size());

  std::sort(results.begin(), results.end());
  GenerateResult(config_.input_config().path(), config_.output_config().path(),
                 selected_keys, results, false, false, false);
}

}  // namespace psi::ecdh
