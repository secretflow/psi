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

#include "psi/algorithm/ecdh/ub_psi/client.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#include "yacl/crypto/rand/rand.h"

#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/random_str.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {

EcdhUbPsiClient::EcdhUbPsiClient(const v2::UbPsiConfig& config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiClient(config, std::move(lctx)) {}

void EcdhUbPsiClient::Init() {
  YACL_ENFORCE(config_.mode() != v2::UbPsiConfig::MODE_UNSPECIFIED &&
               config_.mode() != v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE);

  if (lctx_) {
    // Test connection.
    lctx_->ConnectToMesh();

    psi_options_.cache_transfer_link = lctx_;
    psi_options_.online_link = lctx_->Spawn();
  }

  dir_resource_ = ResourceManager::GetInstance().AddDirResouce(
      std::filesystem::temp_directory_path() / GetRandomString());
  join_processor_ = JoinProcessor::Make(config_, dir_resource_->Path());
}

void EcdhUbPsiClient::OfflineGenCache() { YACL_THROW("unsupported."); }

std::string EcdhUbPsiClient::GetServerCachePath() const {
  return std::filesystem::path(config_.cache_path()) / "server_cache";
}

void EcdhUbPsiClient::OfflineTransferCache() {
  std::shared_ptr<EcdhOprfPsiClient> ub_psi_client_transfer_cache =
      std::make_shared<EcdhOprfPsiClient>(psi_options_);

  if (std::filesystem::exists(GetServerCachePath())) {
    SPDLOG_INFO("old cache file exists, remove {}", GetServerCachePath());
    std::filesystem::remove_all(config_.cache_path());
  }

  auto peer_ec_point_store = std::make_shared<UbPsiClientCacheFileStore>(
      GetServerCachePath(), ub_psi_client_transfer_cache->GetCompareLength());

  ub_psi_client_transfer_cache->RecvFinalEvaluatedItems(peer_ec_point_store);

  peer_ec_point_store->Flush();

  yacl::link::Barrier(lctx_, "ubpsi_offline_transfer_cache");

  report_.set_original_count(peer_ec_point_store->ItemCount());
  report_.set_intersection_count(-1);
}

void EcdhUbPsiClient::Offline() {
  SyncWait(lctx_, [this]() { OfflineTransferCache(); });
}

// memory cost: csv_batch(1M * lineBytes) + cached_ec_point_store(items * 32B *
// 2)
//   + peer_ec_point_store(items * 32B * 2) + send&recv(items * 8B * 2) +
//   indexes(items * 2 * 8B)
//   ~= 160 * items + constants(1G)
void EcdhUbPsiClient::Online() {
  SyncWait(lctx_, [&]() {
    auto private_key = yacl::crypto::SecureRandBytes(kEccKeySize);
    std::shared_ptr<EcdhOprfPsiClient> dh_oprf_psi_client_online =
        std::make_shared<EcdhOprfPsiClient>(psi_options_, private_key);

    auto key_info = join_processor_->GetUniqueKeysInfo();
    report_.set_original_key_count(key_info->KeyCnt());
    report_.set_original_count(key_info->OriginCnt());

    auto batch_provider = key_info->GetBatchProvider();

    auto self_ec_point_store = std::make_shared<UbPsiClientCacheMemoryStore>();

    auto peer_ec_point_store = std::make_shared<UbPsiClientCacheFileStore>(
        GetServerCachePath(), dh_oprf_psi_client_online->GetCompareLength());

    SPDLOG_INFO("online protocol CachedCsvCipherStore: {}",
                GetServerCachePath());

    std::future<size_t> f_client_send_blind = std::async([&] {
      return dh_oprf_psi_client_online->SendBlindedItems(
          batch_provider, config_.server_get_result());
    });

    dh_oprf_psi_client_online->RecvEvaluatedItems(self_ec_point_store);

    self_ec_point_store->Flush();

    f_client_send_blind.get();

    auto intersection_info = ComputeIndicesWithDupCnt(
        self_ec_point_store, peer_ec_point_store, psi_options_.batch_size);

    if (config_.server_get_result()) {
      // How to get exact dup count of each index
      dh_oprf_psi_client_online->SendServerCacheIndexes(
          intersection_info.peer_indices, intersection_info.self_indices);
    }

    if (config_.client_get_result()) {
      MemoryIndexReader index_reader(intersection_info.self_indices,
                                     intersection_info.peer_dup_cnt);
      auto stat = join_processor_->DealResultIndex(index_reader);
      SPDLOG_INFO("join stat: {}", stat.ToString());
      report_.set_intersection_count(stat.self_intersection_count);
      report_.set_intersection_key_count(stat.inter_unique_cnt);

      join_processor_->GenerateResult(peer_ec_point_store->PeerCount() -
                                      stat.peer_intersection_count);
    }
  });
}

}  // namespace psi::ecdh
