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

#include "experiment/psi/threshold_ub_psi/client.h"

#include "experiment/psi/threshold_ub_psi/common.h"

#include "psi/utils/sync.h"

namespace psi::ecdh {
ThresholdEcdhUbPsiClient::ThresholdEcdhUbPsiClient(
    const v2::UbPsiConfig &config, std::shared_ptr<yacl::link::Context> lctx)
    : EcdhUbPsiClient(config, std::move(lctx)) {}

void ThresholdEcdhUbPsiClient::Online() {
  SyncWait(lctx_, [&]() {
    // Since the server will shuffle y^rs, the client needs to use the same
    // private key; otherwise, it will be impossible to compute (y^rs)^(1/r).
    // Assume y is the client's data, r is the client's private key, and s is
    // the server's private key.
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

    // client send first blinded items y^r
    std::future<size_t> f_client_send_blind = std::async([&] {
      return dh_oprf_psi_client_online->SendBlindedItems(
          batch_provider, config_.server_get_result());
    });

    // client recv evaluated items y^rs
    dh_oprf_psi_client_online->RecvEvaluatedItems(self_ec_point_store);

    self_ec_point_store->Flush();

    f_client_send_blind.get();

    // client compute intersection by comparing x^rs with y^sr
    auto intersection_info = ComputeIndicesWithDupCnt(
        self_ec_point_store, peer_ec_point_store, psi_options_.batch_size);

    uint32_t threshold = config_.intersection_threshold();
    uint32_t real_intersection_unique_key_count =
        intersection_info.self_indices.size();
    uint32_t final_intersection_unique_key_count =
        std::min(threshold, real_intersection_unique_key_count);

    // Limit the size of the intersection based on the threshold
    ResizeIntersection(intersection_info, final_intersection_unique_key_count);

    std::vector<uint32_t> client_restored_indexes;

    // The indexes are restored only when the client needs to obtain the
    // intersection
    if (config_.client_get_result()) {
      dh_oprf_psi_client_online->SendClientShuffledIndexes(
          intersection_info.self_indices);
      client_restored_indexes =
          dh_oprf_psi_client_online->RecvClientRestoredIndexes();
      YACL_ENFORCE(
          client_restored_indexes.size() == final_intersection_unique_key_count,
          "index size not match");
    }

    if (config_.server_get_result()) {
      // The client_index sent to the server does not need to be restored
      dh_oprf_psi_client_online->SendServerCacheIndexes(
          intersection_info.peer_indices, intersection_info.self_indices);
      dh_oprf_psi_client_online->SendIntersectionUniqueKeyCount(
          real_intersection_unique_key_count);
    }

    if (config_.client_get_result()) {
      MemoryIndexReader index_reader(client_restored_indexes,
                                     intersection_info.peer_dup_cnt);
      auto stat = join_processor_->DealResultIndex(index_reader);
      SPDLOG_INFO("join stat: {}", stat.ToString());
      report_.set_intersection_count(stat.self_intersection_count);
      report_.set_intersection_key_count(stat.inter_unique_cnt);

      join_processor_->GenerateResult(peer_ec_point_store->PeerCount() -
                                      stat.peer_intersection_count);

      if (!config_.count_path().empty()) {
        SaveIntersectCount(config_.count_path(),
                           real_intersection_unique_key_count,
                           final_intersection_unique_key_count);
      }

      SPDLOG_INFO(
          "The number of unique keys in the actual intersection is {}, the "
          "threshold is {}, and the number of unique keys in the final "
          "intersection is {}.",
          real_intersection_unique_key_count, threshold,
          final_intersection_unique_key_count);
    }
  });
}

void ThresholdEcdhUbPsiClient::ResizeIntersection(
    IntersectionIndexInfo &intersection_info, uint32_t final_count) {
  intersection_info.self_indices.resize(final_count);
  intersection_info.peer_indices.resize(final_count);
  intersection_info.self_dup_cnt.resize(final_count);
  intersection_info.peer_dup_cnt.resize(final_count);
}

}  // namespace psi::ecdh