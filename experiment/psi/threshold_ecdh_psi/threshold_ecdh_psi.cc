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

#include "experiment/psi/threshold_ecdh_psi/threshold_ecdh_psi.h"

#include <algorithm>
#include <vector>

#include "yacl/crypto/rand/rand.h"

namespace psi::ecdh {

constexpr int kLogBatchInterval = 10;

namespace {
uint64_t GetProcessedItemCnt(const EcdhPsiOptions& options,
                             const EcdhPsiContext& handler) {
  uint64_t processed_item_cnt = 0;
  if (handler.SelfCanTouchResults() && handler.PeerCanTouchResults()) {
    processed_item_cnt =
        std::min(options.recovery_manager->ecdh_dual_masked_cnt_from_peer(),
                 options.recovery_manager->checkpoint()
                     .ecdh_dual_masked_item_self_count());
  } else if (handler.SelfCanTouchResults() && !handler.PeerCanTouchResults()) {
    processed_item_cnt = options.recovery_manager->checkpoint()
                             .ecdh_dual_masked_item_self_count();
  } else {
    processed_item_cnt =
        options.recovery_manager->ecdh_dual_masked_cnt_from_peer();
  }

  SPDLOG_INFO("processed_item_cnt = {}", processed_item_cnt);

  return processed_item_cnt;
}
}  // namespace

ThresholdEcdhPsiCtx::ThresholdEcdhPsiCtx(const EcdhPsiOptions& options)
    : EcdhPsiContext(options) {}

void ThresholdEcdhPsiCtx::MaskAndShufflePeer(
    const std::shared_ptr<ThresholdEcdhPsiCache>& cache) {
  size_t batch_count = 0;
  size_t item_count = 0;

  while (true) {
    std::vector<std::string> peer_items;
    std::vector<std::string> dual_masked_peers;
    std::vector<std::string> shuffled_items;
    std::unordered_map<uint32_t, uint32_t> duplicate_item_cnt;
    const auto recv_tag = fmt::format("THRESHOLDECDHPSI:X^A:{}", batch_count);

    // Sender recv x^a.
    RecvBatch(&peer_items, &duplicate_item_cnt, batch_count, recv_tag);
    if (!duplicate_item_cnt.empty()) {
      SPDLOG_INFO("recv extra item cnt: {}", duplicate_item_cnt.size());
    }

    auto peer_points = options_.ecc_cryptor->DeserializeEcPoints(peer_items);

    // Sender compute and shuffle (x^a)^b.
    if (!peer_items.empty()) {
      const auto& masked_points = options_.ecc_cryptor->EccMask(peer_points);
      for (uint32_t i = 0; i != peer_points.size(); ++i) {
        const auto masked =
            options_.ecc_cryptor->SerializeEcPoint(masked_points[i]);
        // In the final comparison, we only send & compare `kFinalCompareBytes`
        // number of bytes.
        std::string cipher(
            masked.data<char>() + masked.size() - options_.dual_mask_size,
            options_.dual_mask_size);

        dual_masked_peers.emplace_back(std::move(cipher));
      }

      // Sender shuffle receiver's dual masked items.
      std::vector<uint32_t> shuffled_indices(dual_masked_peers.size());
      std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0);
      yacl::crypto::YaclReplayUrbg<uint32_t> gen(yacl::crypto::SecureRandSeed(),
                                                 yacl::crypto::SecureRandU64());
      std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), gen);

      std::vector<uint32_t> dup_cnts(dual_masked_peers.size());
      shuffled_items.resize(dual_masked_peers.size());

      // Convert unordered_map to vector to improve efficiency.
      for (auto& [k, v] : duplicate_item_cnt) {
        dup_cnts[k] = v;
      }

      // Sender store indexes, shuffled indexes and duplicate cnt in cache.
      for (size_t i = 0; i < dual_masked_peers.size(); ++i) {
        shuffled_items[i] = std::move(dual_masked_peers[shuffled_indices[i]]);
        cache->SaveData(item_count + i, item_count + shuffled_indices[i],
                        dup_cnts[i]);
      }
    }

    auto target_rank_str = [&, this]() {
      return options_.target_rank == yacl::link::kAllRank
                 ? "all"
                 : std::to_string(options_.target_rank);
    };

    if (batch_count == 0) {
      SPDLOG_INFO("SendDualMaskedItems to peer: {}, batch={}, begin...",
                  target_rank_str(), batch_count);
    }

    // Sender send shuffle items to receiver and call non-block to avoid
    // blocking each other with MaskSelf
    const auto send_tag = fmt::format("THRESHOLDECDHPSI:X^A^B:{}", batch_count);
    SendDualMaskedBatchNonBlock(shuffled_items, batch_count, send_tag);
    SPDLOG_INFO("SendDualMaskedItems to peer: {}, batch={}, end...",
                target_rank_str(), batch_count);

    // All the items have been processed.
    if (peer_items.empty()) {
      SPDLOG_INFO("SendDualMaskedItems to peer: {}, batch_count={}, finished.",
                  target_rank_str(), batch_count);
      SPDLOG_INFO(
          "MaskAndShufflePeer:{} --finished, batch_count={}, "
          "peer_item_count={}",
          Id(), batch_count, item_count);

      // The size of the receiver's set is recorded.
      if (options_.statistics) {
        options_.statistics->peer_item_count = item_count;
      }
      break;
    }
    item_count += peer_items.size();
    batch_count++;

    if (batch_count % kLogBatchInterval == 0) {
      SPDLOG_INFO("MaskAndShufflePeer:{}, batch_count={}, peer_item_count={}",
                  Id(), batch_count, item_count);
    }
  }
}

void ThresholdEcdhPsiCtx::RecvAndMaskPeer(
    const std::shared_ptr<IEcPointStore>& peer_ec_point_store) {
  size_t batch_count = 0;
  size_t item_count = 0;

  while (true) {
    std::vector<std::string> peer_items;
    std::unordered_map<uint32_t, uint32_t> duplicate_item_cnt;
    const auto tag = fmt::format("THRESHOLDECDHPSI:Y^B:{}", batch_count);

    // Receiver recv y^b.
    RecvBatch(&peer_items, &duplicate_item_cnt, batch_count, tag);
    if (!duplicate_item_cnt.empty()) {
      SPDLOG_INFO("recv extra item cnt: {}", duplicate_item_cnt.size());
    }

    auto peer_points = options_.ecc_cryptor->DeserializeEcPoints(peer_items);

    // Receiver compute (y^b)^a.
    if (!peer_items.empty()) {
      const auto& masked_points = options_.ecc_cryptor->EccMask(peer_points);
      for (uint32_t i = 0; i != peer_points.size(); ++i) {
        const auto masked =
            options_.ecc_cryptor->SerializeEcPoint(masked_points[i]);
        // In the final comparison, we only send & compare `kFinalCompareBytes`
        // number of bytes.
        std::string cipher(
            masked.data<char>() + masked.size() - options_.dual_mask_size,
            options_.dual_mask_size);

        // Store cipher of peer items (y^{ba}) for later intersection compute.
        peer_ec_point_store->Save(cipher, duplicate_item_cnt[i]);
      }

      if (options_.recovery_manager) {
        peer_ec_point_store->Flush();
        options_.recovery_manager->UpdateEcdhDualMaskedItemPeerCount(
            peer_ec_point_store->ItemCount());
      }
    }

    // The dummy batch indicates the end of data stream.
    if (peer_items.empty()) {
      SPDLOG_INFO(
          "RecvAndMaskPeer:{} --finished, batch_count={}, peer_item_count={}",
          Id(), batch_count, item_count);
      // The size of the sender's set is recorded.
      if (options_.statistics) {
        options_.statistics->peer_item_count = item_count;
      }
      break;
    }

    item_count += peer_items.size();
    batch_count++;

    if (batch_count % kLogBatchInterval == 0) {
      SPDLOG_INFO("RecvAndMaskPeer:{}, batch_count={}, self_item_count={}",
                  Id(), batch_count, item_count);
    }
  }
}

void RunSender(const EcdhPsiOptions& options,
               const std::filesystem::path& cache_path,
               const std::shared_ptr<IBasicBatchProvider>& batch_provider,
               const std::shared_ptr<IEcPointStore>& self_ec_point_store,
               const std::shared_ptr<IEcPointStore>& peer_ec_point_store) {
  YACL_ENFORCE(options.link_ctx->WorldSize() == 2);
  YACL_ENFORCE(batch_provider != nullptr && self_ec_point_store != nullptr &&
               peer_ec_point_store != nullptr);

  ThresholdEcdhPsiCtx handler(options);
  handler.CheckConfig();

  std::shared_ptr<ThresholdEcdhPsiCache> cache =
      std::make_shared<ThresholdEcdhPsiCache>(cache_path);

  uint64_t processed_item_cnt = 0;
  if (options.recovery_manager) {
    processed_item_cnt = GetProcessedItemCnt(options, handler);
  }

  // Sender mask self items then send them to receiver.
  std::future<void> f_mask_self = std::async([&] {
    SPDLOG_INFO("ID {}: MaskSelf begin...", handler.Id());
    handler.MaskSelf(batch_provider, processed_item_cnt);
    SPDLOG_INFO("ID {}: MaskSelf finished.", handler.Id());
  });

  // Sender recv receiver's masked items, mask them again then shuffle and send
  // them back.
  std::future<void> f_mask_peer = std::async([&] {
    SPDLOG_INFO("ID {}: MaskAndShufflePeer begin...", handler.Id());
    handler.MaskAndShufflePeer(cache);
    SPDLOG_INFO("ID {}: MaskAndShufflePeer finished.", handler.Id());
  });

  std::vector<std::future<void>> tasks;
  tasks.emplace_back(std::move(f_mask_self));
  tasks.emplace_back(std::move(f_mask_peer));

  std::vector<std::exception_ptr> exceptions(2, nullptr);
  std::vector<std::string> task_names = {"MaskSelf", "MaskAndShufflePeer"};

  for (size_t i = 0; i < tasks.size(); ++i) {
    try {
      tasks[i].get();
    } catch (const std::exception& e) {
      exceptions[i] = std::current_exception();
      SPDLOG_ERROR("ID {}: Error in {}: {}", handler.Id(), task_names[i],
                   e.what());
    }
  }

  for (auto& exptr : exceptions) {
    if (exptr) {
      std::rethrow_exception(exptr);
    }
  }
}

void RunReceiver(const EcdhPsiOptions& options,
                 const std::shared_ptr<IBasicBatchProvider>& batch_provider,
                 const std::shared_ptr<IEcPointStore>& self_ec_point_store,
                 const std::shared_ptr<IEcPointStore>& peer_ec_point_store) {
  YACL_ENFORCE(options.link_ctx->WorldSize() == 2);
  YACL_ENFORCE(batch_provider != nullptr && self_ec_point_store != nullptr &&
               peer_ec_point_store != nullptr);

  ThresholdEcdhPsiCtx handler(options);
  handler.CheckConfig();

  uint64_t processed_item_cnt = 0;
  if (options.recovery_manager) {
    processed_item_cnt = GetProcessedItemCnt(options, handler);
  }

  // Receiver mask self items then send them to sender.
  std::future<void> f_mask_self = std::async([&] {
    SPDLOG_INFO("ID {}: MaskSelf begin...", handler.Id());
    handler.MaskSelf(batch_provider, processed_item_cnt);
    SPDLOG_INFO("ID {}: MaskSelf finished.", handler.Id());
  });

  // Receiver recv sender's masked items, mask them again.
  std::future<void> f_recv_mask_peer = std::async([&] {
    SPDLOG_INFO("ID {}: RecvAndMaskPeer begin...", handler.Id());
    handler.RecvAndMaskPeer(peer_ec_point_store);
    SPDLOG_INFO("ID {}: RecvAndMaskPeer finished.", handler.Id());
  });

  // Receiver recv dual masked self items from sender.
  std::future<void> f_recv_masked_self = std::async([&] {
    SPDLOG_INFO("ID {}: RecvDualMaskedSelf begin...", handler.Id());
    handler.RecvDualMaskedSelf(self_ec_point_store);
    SPDLOG_INFO("ID {}: RecvDualMaskedSelf finished.", handler.Id());
  });

  std::vector<std::future<void>> tasks;
  tasks.emplace_back(std::move(f_mask_self));
  tasks.emplace_back(std::move(f_recv_mask_peer));
  tasks.emplace_back(std::move(f_recv_masked_self));

  std::vector<std::exception_ptr> exceptions(3, nullptr);
  std::vector<std::string> task_names = {"MaskSelf", "RecvAndMaskPeer",
                                         "RecvDualMaskedSelf"};

  for (size_t i = 0; i < tasks.size(); ++i) {
    try {
      tasks[i].get();
    } catch (const std::exception& e) {
      exceptions[i] = std::current_exception();
      SPDLOG_ERROR("ID {}: Error in {}: {}", handler.Id(), task_names[i],
                   e.what());
    }
  }

  for (auto& exptr : exceptions) {
    if (exptr) {
      std::rethrow_exception(exptr);
    }
  }
}
}  // namespace psi::ecdh