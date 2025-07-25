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

#include "experiment/psi/threshold_ub_psi/server.h"

#include "experiment/psi/threshold_ub_psi/common.h"
#include "experiment/psi/threshold_ub_psi/threshold_ecdh_oprf_psi.h"

#include "psi/utils/ec.h"
#include "psi/utils/sync.h"

namespace psi::ecdh {

ThresholdEcdhUbPsiServer::ThresholdEcdhUbPsiServer(
    const v2::UbPsiConfig& config, std::shared_ptr<yacl::link::Context> lctx)
    : EcdhUbPsiServer(config, std::move(lctx)) {}

void ThresholdEcdhUbPsiServer::Online() {
  SyncWait(lctx_, [&]() {
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

    std::shared_ptr<ThresholdEcdhOprfPsiServer> server =
        std::make_shared<ThresholdEcdhOprfPsiServer>(psi_options_,
                                                     server_private_key);

    // server recv client's first blinded items y^r and evaluate items y^rs
    ThresholdEcdhOprfPsiServer::PeerCntInfo peer_cnt_info;
    peer_cnt_info = server->RecvBlindAndShuffleSendEvaluate();
    SPDLOG_INFO("End send shuffle evaluate items.");

    // The indexes are restored only when the client needs to obtain the
    // intersection
    if (config_.client_get_result()) {
      server->RecvAndRestoreIndexes(peer_cnt_info.peer_unique_cnt);
    }

    if (!config_.server_get_result()) {
      return;
    }

    auto index_info = server->RecvCacheIndexes();
    uint32_t real_intersection_unique_key_count =
        server->RecvIntersectionUniqueKeyCount();
    SPDLOG_INFO("End recv cached indexes and intersection count.");

    // Check whether the intersection size meets expectations
    uint32_t threshold = config_.intersection_threshold();
    uint32_t target_intersection_unique_key_count =
        std::min(threshold, real_intersection_unique_key_count);
    uint32_t final_intersection_unique_key_count =
        index_info.client_index.size();

    YACL_ENFORCE(final_intersection_unique_key_count ==
                     target_intersection_unique_key_count,
                 "The threshold has not taken effect, target size is {}, "
                 "but the final size is {}",
                 target_intersection_unique_key_count,
                 final_intersection_unique_key_count);

    // server computes the duplicate counts of the intersection elements in the
    // client's set
    std::unordered_map<uint32_t, uint32_t> shuffle_index_cnt_map;
    for (size_t i = 0; i != index_info.cache_index.size(); ++i) {
      shuffle_index_cnt_map[index_info.cache_index[i]] =
          peer_cnt_info.peer_dup_cnt[index_info.client_index[i]];
    }

    // server restored the shuffled intersection indexes
    auto index_with_cnt = TransCacheIndexesToRowIndexs(shuffle_index_cnt_map);

    MemoryIndexReader reader(index_with_cnt.index, index_with_cnt.peer_dup_cnt);

    // server deals with the intersection and save the result
    auto stat = join_processor_->DealResultIndex(reader);
    SPDLOG_INFO("join stat: {}", stat.ToString());
    report_.set_intersection_count(stat.self_intersection_count);
    report_.set_intersection_key_count(stat.inter_unique_cnt);

    join_processor_->GenerateResult(peer_cnt_info.peer_total_cnt -
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
  });
}
}  // namespace psi::ecdh