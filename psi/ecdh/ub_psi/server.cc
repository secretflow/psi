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

#include "psi/ecdh/ub_psi/server.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "yacl/crypto/rand/rand.h"

#include "psi/utils/batch_provider_impl.h"
#include "psi/utils/ec.h"
#include "psi/utils/random_str.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {

EcdhUbPsiServer::EcdhUbPsiServer(const v2::UbPsiConfig& config,
                                 std::shared_ptr<yacl::link::Context> lctx)
    : AbstractUbPsiServer(config, std::move(lctx)) {}

void EcdhUbPsiServer::Init() {
  YACL_ENFORCE(config_.mode() != v2::UbPsiConfig::MODE_UNSPECIFIED);

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

std::shared_ptr<IBasicBatchProvider> EcdhUbPsiServer::GetInputCsvProvider() {
  return join_processor_->GetUniqueKeysInfo()->GetKeysProviderWithDupCnt(
      std::max(1000000ul, psi_options_.batch_size));
}

std::shared_ptr<EcdhOprfPsiServer> EcdhUbPsiServer::GetOprfServer(
    const std::vector<uint8_t>& private_key) {
  return std::make_shared<EcdhOprfPsiServer>(psi_options_, private_key);
}

void EcdhUbPsiServer::OfflineGenCache() {
  std::vector<uint8_t> server_private_key;
  if (!config_.server_secret_key_path().empty()) {
    server_private_key = ReadEcSecretKeyFile(config_.server_secret_key_path());
  } else {
    server_private_key = yacl::crypto::SecureRandBytes(kEccKeySize);
  }

  auto server = GetOprfServer(server_private_key);
  std::vector<std::string> selected_keys(config_.keys().begin(),
                                         config_.keys().end());
  std::shared_ptr<IUbPsiCache> ub_cache = std::make_shared<UbPsiCache>(
      config_.cache_path(), server->GetCompareLength(), selected_keys,
      server_private_key);

  auto csv_batch_provider = GetInputCsvProvider();
  std::shared_ptr<IShuffledBatchProvider> shuffle_batch_provider =
      std::make_shared<SimpleShuffledBatchProvider>(csv_batch_provider,
                                                    psi_options_.batch_size);
  size_t self_items_count =
      server->FullEvaluate(shuffle_batch_provider, ub_cache);

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(-1);
}

std::shared_ptr<UbPsiCacheProvider> EcdhUbPsiServer::GetCacheProvider() {
  YACL_ENFORCE(!config_.cache_path().empty());
  return std::make_shared<UbPsiCacheProvider>(config_.cache_path(),
                                              psi_options_.batch_size);
}

void EcdhUbPsiServer::OfflineTransferCache() {
  auto batch_provider = GetCacheProvider();
  auto ub_psi_server_transfer_cache =
      GetOprfServer(batch_provider->GetCachePrivateKey());

  size_t self_items_count =
      ub_psi_server_transfer_cache->SendFinalEvaluatedItems(batch_provider);

  yacl::link::Barrier(lctx_, "ubpsi_offline_transfer_cache");

  report_.set_original_count(self_items_count);
  report_.set_intersection_count(-1);
}

void EcdhUbPsiServer::Offline() {
  OfflineGenCache();
  OfflineTransferCache();
}

EcdhUbPsiServer::IndexWithCnt EcdhUbPsiServer::TransCacheIndexesToRowIndexs(
    const std::unordered_map<uint32_t, uint32_t>& shuffle_index_cnt_map) {
  IndexWithCnt row_indexes;

  row_indexes.index.reserve(shuffle_index_cnt_map.size());
  row_indexes.peer_dup_cnt.reserve(shuffle_index_cnt_map.size());

  auto cache_provider = GetCacheProvider();
  while (true) {
    auto shuffled_batch = cache_provider->ReadNextShuffledBatch();
    SPDLOG_DEBUG("shuffled_batch.size={}", shuffled_batch.batch_items.size());

    auto& items = shuffled_batch.batch_items;
    auto& index = shuffled_batch.batch_indices;
    auto& shuffle = shuffled_batch.shuffled_indices;

    if (items.empty()) {
      break;
    }

    for (size_t i = 0; i != index.size(); ++i) {
      auto iter = shuffle_index_cnt_map.find(index[i]);
      if (iter == shuffle_index_cnt_map.end()) {
        continue;
      }
      row_indexes.index.push_back(shuffle[i]);
      row_indexes.peer_dup_cnt.push_back(iter->second);
    }
  }
  return row_indexes;
}

// memory cost: csv_batch(1M * lineBytes) + cached_ec_point_store(items * 32B *
// 2) + send&recv(items * 8B * 2)
//   ~= 80 * items + constants(1G)
void EcdhUbPsiServer::Online() {
  std::shared_ptr<yacl::link::Context> sync_lctx = lctx_->Spawn();

  auto online_proc = std::async([&]() {
    std::vector<uint8_t> server_private_key;
    if (!config_.server_secret_key_path().empty()) {
      server_private_key =
          ReadEcSecretKeyFile(config_.server_secret_key_path());
    } else {
      auto cache = GetCacheProvider();
      server_private_key = cache->GetCachePrivateKey();
    }

    auto keys_info = join_processor_->GetUniqueKeysInfo();
    report_.set_original_count(keys_info->OriginCnt());
    report_.set_original_key_count(keys_info->KeyCnt());

    std::shared_ptr<EcdhOprfPsiServer> server =
        GetOprfServer(server_private_key);

    EcdhOprfPsiServer::PeerCntInfo peer_cnt_info;
    if (config_.client_get_result()) {
      peer_cnt_info = server->RecvBlindAndSendEvaluate();
      SPDLOG_INFO("End send evaluate items.");
    } else {
      peer_cnt_info = server->RecvBlindAndShuffleSendEvaluate();
      SPDLOG_INFO("End send shuffle evaluate items.");
    }

    if (!config_.server_get_result()) {
      return;
    }

    auto index_info = server->RecvCacheIndexes();
    SPDLOG_INFO("End recv cached indexe.");

    std::unordered_map<uint32_t, uint32_t> shuffle_index_cnt_map;
    for (size_t i = 0; i != index_info.cache_index.size(); ++i) {
      shuffle_index_cnt_map[index_info.cache_index[i]] =
          peer_cnt_info.peer_dup_cnt[index_info.client_index[i]];
    }

    auto index_with_cnt = TransCacheIndexesToRowIndexs(shuffle_index_cnt_map);

    MemoryIndexReader reader(index_with_cnt.index, index_with_cnt.peer_dup_cnt);

    auto stat = join_processor_->DealResultIndex(reader);
    SPDLOG_INFO("join stat: {}", stat.ToString());
    report_.set_intersection_count(stat.self_intersection_count);
    report_.set_intersection_key_count(stat.inter_unique_cnt);

    join_processor_->GenerateResult(peer_cnt_info.peer_total_cnt -
                                    stat.peer_intersection_count);
  });

  SyncWait(sync_lctx, &online_proc);
}

}  // namespace psi::ecdh
