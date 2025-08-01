// Copyright 2022 Ant Group Co., Ltd.
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

#include "psi/algorithm/ecdh/ub_psi/ecdh_oprf_psi.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <omp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iterator>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "absl/strings/escaping.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/ecdh/ub_psi/ecdh_oprf_selector.h"
#include "psi/cryptor/ecc_utils.h"
#include "psi/utils/communication.h"
#include "psi/utils/serialize.h"

namespace psi::ecdh {

size_t EcdhOprfPsiServer::FullEvaluateAndSend(
    const std::shared_ptr<IShuffledBatchProvider>& batch_provider,
    const std::shared_ptr<IUbPsiCache>& ub_cache) {
  return FullEvaluate(batch_provider, ub_cache, true);
}

size_t EcdhOprfPsiServer::SendFinalEvaluatedItems(
    const std::shared_ptr<IBasicBatchProvider>& batch_provider) {
  size_t items_count = 0;
  size_t batch_count = 0;

  size_t compare_length = oprf_server_->GetCompareLength();

  while (true) {
    PsiDataBatch batch;
    auto [items, dup_cnt] = batch_provider->ReadNextBatchWithDupCnt();

    batch.is_last_batch = items.empty();

    if (!batch.is_last_batch) {
      batch.flatten_bytes.reserve(items.size() * compare_length);

      for (const auto& item : items) {
        batch.flatten_bytes.append(item);
      }
      batch.duplicate_item_cnt = dup_cnt;
    }

    // Send x^a.
    const auto tag =
        fmt::format("EcdhOprfPSI:FinalEvaluatedItems:{}", batch_count);
    options_.cache_transfer_link->SendAsyncThrottled(
        options_.cache_transfer_link->NextRank(), batch.Serialize(), tag);

    if (batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}", __func__,
                  batch_count);
      break;
    }

    items_count += items.size();
    batch_count++;
  }

  SPDLOG_INFO("{} finished, batch_count={}, item_count={}", __func__,
              batch_count, items_count);

  return items_count;
}

size_t EcdhOprfPsiServer::FullEvaluate(
    const std::shared_ptr<IShuffledBatchProvider>& batch_provider,
    const std::shared_ptr<IUbPsiCache>& ub_cache, bool send_flag) {
  size_t items_count = 0;
  size_t batch_count = 1;
  size_t compare_length = oprf_server_->GetCompareLength();

  bool stop_flag = false;

  batch_count = 0;
  std::vector<std::string> batch_items;
  std::vector<size_t> batch_indices;
  std::vector<size_t> shuffle_indices;
  PsiDataBatch batch;
  size_t i;
  size_t local_batch_count = 0;
  while (!stop_flag) {
    if (stop_flag) {
      break;
    }

    auto shuffled_batch = batch_provider->ReadNextShuffledBatch();
    auto& batch_items = shuffled_batch.batch_items;
    auto& batch_indices = shuffled_batch.batch_indices;
    auto& shuffle_indices = shuffled_batch.shuffled_indices;

    batch.is_last_batch = batch_items.empty();

    if (batch_items.empty()) {
      stop_flag = true;
    } else {
      items_count += batch_items.size();
      batch_count++;
      if ((batch_count % 1000) == 0) {
        SPDLOG_INFO("batch_count: {}, items: {}", batch_count, items_count);
      }
      local_batch_count = batch_count;
    }

    if (batch_items.empty()) {
      break;
    }

    batch.flatten_bytes.reserve(batch_items.size() * compare_length);
    batch.duplicate_item_cnt.clear();
    for (size_t i = 0; i != shuffled_batch.dup_cnts.size(); i++) {
      if (shuffled_batch.dup_cnts[i] > 0) {
        batch.duplicate_item_cnt[i] = shuffled_batch.dup_cnts[i];
      }
    }

    std::vector<std::string> batch_items_masked(batch_items.size());
    yacl::parallel_for(0, batch_items.size(), [&](size_t begin, size_t end) {
      for (auto j = begin; j < end; ++j) {
        batch_items_masked[j] = oprf_server_->SimpleEvaluate(batch_items[j]);
      }
    });

    batch.flatten_bytes = batch_items_masked[0];
    for (i = 1; i < batch_items.size(); i++) {
      batch.flatten_bytes.append(batch_items_masked[i]);
    }

    if (send_flag) {
      // Send x^a.
      options_.cache_transfer_link->SendAsyncThrottled(
          options_.cache_transfer_link->NextRank(), batch.Serialize(),
          fmt::format("EcdhOprfPSI:FinalEvaluatedItems:{}", local_batch_count));
    }

    if (ub_cache != nullptr) {
      for (size_t i = 0; i < batch_items.size(); i++) {
        std::string cache_data =
            batch.flatten_bytes.substr(i * compare_length, compare_length);
        ub_cache->SaveData(cache_data, batch_indices[i], shuffle_indices[i],
                           shuffled_batch.dup_cnts[i]);
      }
    }
  }

  if (send_flag) {
    batch.is_last_batch = true;
    batch.flatten_bytes.resize(0);
    batch.duplicate_item_cnt.clear();
    options_.cache_transfer_link->SendAsyncThrottled(
        options_.cache_transfer_link->NextRank(), batch.Serialize(),
        fmt::format("EcdhOprfPSI last batch,FinalEvaluatedItems:{}",
                    batch_count));
  }

  if (ub_cache != nullptr) {
    ub_cache->Flush();
  }

  SPDLOG_INFO("{} finished, batch_count={} items_count={}", __func__,
              batch_count, items_count);

  return items_count;
}

EcdhOprfPsiServer::PeerCntInfo EcdhOprfPsiServer::RecvBlindAndSendEvaluate() {
  PeerCntInfo cnt_info;
  size_t batch_count = 0;
  size_t ec_point_length = oprf_server_->GetEcPointLength();

  while (true) {
    const auto tag = fmt::format("EcdhOprfPSI:BlindItems:{}", batch_count);
    PsiDataBatch blinded_batch = PsiDataBatch::Deserialize(
        options_.online_link->Recv(options_.online_link->NextRank(), tag));

    PsiDataBatch evaluated_batch;
    evaluated_batch.is_last_batch = blinded_batch.is_last_batch;

    const auto tag_send =
        fmt::format("EcdhOprfPSI:EvaluatedItems:{}", batch_count);

    if (blinded_batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}", __func__,
                  batch_count);

      options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                               evaluated_batch.Serialize(),
                                               tag_send);
      break;
    }

    // Fetch blinded y^r.
    YACL_ENFORCE(blinded_batch.flatten_bytes.size() % ec_point_length == 0);
    size_t num_items = blinded_batch.flatten_bytes.size() / ec_point_length;

    std::vector<std::string> blinded_items(num_items);
    for (size_t idx = 0; idx < num_items; ++idx) {
      blinded_items[idx] = blinded_batch.flatten_bytes.substr(
          idx * ec_point_length, ec_point_length);
    }
    // (x^r)^s
    std::vector<std::string> evaluated_items =
        oprf_server_->Evaluate(blinded_items);

    evaluated_batch.flatten_bytes.reserve(evaluated_items.size() *
                                          ec_point_length);
    for (const auto& item : evaluated_items) {
      evaluated_batch.flatten_bytes.append(item);
    }

    for (auto [index, dup_cnt] : blinded_batch.duplicate_item_cnt) {
      cnt_info.peer_dup_cnt[index + cnt_info.peer_unique_cnt] = dup_cnt;
      cnt_info.peer_total_cnt += dup_cnt;
    }

    options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                             evaluated_batch.Serialize(),
                                             tag_send);
    cnt_info.peer_unique_cnt += num_items;
    batch_count++;
  }
  cnt_info.peer_total_cnt += cnt_info.peer_unique_cnt;
  SPDLOG_INFO("{} finished, batch_count={}, unique_items: {}, total_items: {}",
              __func__, batch_count, cnt_info.peer_unique_cnt,
              cnt_info.peer_total_cnt);
  return cnt_info;
}

EcdhOprfPsiServer::PeerCntInfo
EcdhOprfPsiServer::RecvBlindAndShuffleSendEvaluate() {
  PeerCntInfo cnt_info;
  size_t batch_count = 0;
  std::unordered_map<uint32_t, uint32_t> peer_dup_cnt;

  size_t ec_point_length = oprf_server_->GetEcPointLength();

  std::vector<std::string> evaluated_items;

  while (true) {
    const auto tag = fmt::format("EcdhOprfPSI:BlindItems:{}", batch_count);
    PsiDataBatch blinded_batch = PsiDataBatch::Deserialize(
        options_.online_link->Recv(options_.online_link->NextRank(), tag));

    if (blinded_batch.is_last_batch) {
      break;
    }

    // Fetch blinded y^r.
    YACL_ENFORCE(blinded_batch.flatten_bytes.size() % ec_point_length == 0);
    size_t num_items = blinded_batch.flatten_bytes.size() / ec_point_length;

    // TODO(huocun): can be make views, avoid copy.
    std::vector<std::string> blinded_items(num_items);
    for (size_t idx = 0; idx < num_items; ++idx) {
      blinded_items[idx] = blinded_batch.flatten_bytes.substr(
          idx * ec_point_length, ec_point_length);
    }
    // (x^r)^s
    std::vector<std::string> batch_evaluated_items =
        oprf_server_->Evaluate(blinded_items);

    // evaluated_items is scoped and will be destructed soon
    for (auto& item : batch_evaluated_items) {
      evaluated_items.emplace_back(std::move(item));
    }

    for (auto [index, dup_cnt] : blinded_batch.duplicate_item_cnt) {
      peer_dup_cnt[index + cnt_info.peer_unique_cnt] = dup_cnt;
      cnt_info.peer_total_cnt += dup_cnt;
    }

    batch_count++;
    cnt_info.peer_unique_cnt += num_items;
  }
  cnt_info.peer_total_cnt += cnt_info.peer_unique_cnt;
  SPDLOG_INFO(
      "recv Blind finished, batch_count={}, unique_items: {}, total_items: {}",
      batch_count, cnt_info.peer_unique_cnt, cnt_info.peer_total_cnt);

  std::vector<size_t> shuffle_index(evaluated_items.size());
  std::iota(shuffle_index.begin(), shuffle_index.end(), 0);
  yacl::crypto::YaclReplayUrbg<uint32_t> gen(shuffle_seed_, shuffle_counter_);
  std::shuffle(shuffle_index.begin(), shuffle_index.end(), gen);

  cnt_info.peer_dup_cnt.reserve(evaluated_items.size());
  for (size_t i = 0; i != shuffle_index.size(); ++i) {
    cnt_info.peer_dup_cnt[i] = peer_dup_cnt[shuffle_index[i]];
  }

  batch_count = 0;
  size_t item_count = 0;
  for (size_t index = 0; index < evaluated_items.size();
       index += options_.batch_size) {
    auto limit = std::min(index + options_.batch_size, evaluated_items.size());
    PsiDataBatch evaluated_batch;
    evaluated_batch.is_last_batch = false;
    evaluated_batch.flatten_bytes.reserve((limit - index) * ec_point_length);
    item_count += limit - index;
    for (size_t i = index; i < limit; i++) {
      evaluated_batch.flatten_bytes.append(evaluated_items[shuffle_index[i]]);
    }
    options_.online_link->SendAsyncThrottled(
        options_.online_link->NextRank(), evaluated_batch.Serialize(),
        fmt::format("EcdhOprfPSI:EvaluatedItems:{}", batch_count));
    SPDLOG_INFO("send evaluated, batch_count={}, accumulated item_count: {}",
                batch_count, item_count);
    batch_count++;
  }
  PsiDataBatch evaluated_batch;
  evaluated_batch.is_last_batch = true;
  options_.online_link->SendAsyncThrottled(
      options_.online_link->NextRank(), evaluated_batch.Serialize(),
      fmt::format("EcdhOprfPSI:EvaluatedItems:{}", batch_count));

  SPDLOG_INFO("send evaluated finished, batch_count={}, item_count: {}",
              batch_count, item_count);
  return cnt_info;
}

EcdhOprfPsiServer::IndexInfo EcdhOprfPsiServer::RecvCacheIndexes() {
  IndexInfo index_info;
  auto buf = options_.online_link->Recv(options_.online_link->NextRank(),
                                        "cache indexes");
  index_info.cache_index = utils::DeserializeIndexes(buf);
  SPDLOG_INFO("Recv cache index: size: {}", index_info.cache_index.size());
  buf = options_.online_link->Recv(options_.online_link->NextRank(),
                                   "client indexes");
  index_info.client_index = utils::DeserializeIndexes(buf);
  SPDLOG_INFO("Recv client index: size: {}", index_info.client_index.size());

  YACL_ENFORCE_EQ(index_info.client_index.size(),
                  index_info.cache_index.size());

  return index_info;
}

std::pair<std::vector<uint64_t>, size_t>
EcdhOprfPsiServer::RecvIntersectionMaskedItems(
    const std::shared_ptr<IShuffledBatchProvider>& cache_provider) {
  std::unordered_set<std::string> client_masked_items;

  size_t compare_length = oprf_server_->GetCompareLength();
  size_t batch_count = 0;

  while (true) {
    const auto tag = fmt::format("EcdhOprfPSI:batch_count:{}", batch_count);
    PsiDataBatch masked_batch = PsiDataBatch::Deserialize(
        options_.online_link->Recv(options_.online_link->NextRank(), tag));

    if (masked_batch.is_last_batch) {
      break;
    }

    // Fetch blinded y^r.
    YACL_ENFORCE(masked_batch.flatten_bytes.size() % compare_length == 0);
    size_t num_items = masked_batch.flatten_bytes.size() / compare_length;

    std::vector<std::string> batch_masked_items(num_items);
    for (size_t idx = 0; idx < num_items; ++idx) {
      batch_masked_items[idx] = masked_batch.flatten_bytes.substr(
          idx * compare_length, compare_length);
    }

    // batch_masked_items is scoped and will be destructed soon
    client_masked_items.insert(
        std::make_move_iterator(batch_masked_items.begin()),
        std::make_move_iterator(batch_masked_items.end()));

    batch_count++;
  }
  SPDLOG_INFO("Recv intersection masked finished, batch_count={}", batch_count);

  std::vector<uint64_t> indices;

  size_t item_index = 0;
  batch_count = 0;
  size_t compare_thread_num = omp_get_num_procs();

  while (true) {
    auto shuffled_batch = cache_provider->ReadNextShuffledBatch();
    auto& server_masked_items = shuffled_batch.batch_items;
    auto& batch_shuffled_indices = shuffled_batch.shuffled_indices;

    if (server_masked_items.empty()) {
      break;
    }

    YACL_ENFORCE(server_masked_items.size() == batch_shuffled_indices.size());

    size_t compare_size =
        (server_masked_items.size() + compare_thread_num - 1) /
        compare_thread_num;

    std::vector<std::vector<uint64_t>> batch_result(compare_thread_num);

    auto compare_proc = [&](int idx) -> void {
      uint64_t begin = idx * compare_size;
      uint64_t end =
          std::min<uint64_t>(server_masked_items.size(), begin + compare_size);

      for (uint64_t i = begin; i < end; ++i) {
        if (client_masked_items.find(server_masked_items[i]) !=
            client_masked_items.end()) {
          batch_result[idx].push_back(batch_shuffled_indices[i]);
        }
      }
    };

    std::vector<std::future<void>> f_compare(compare_thread_num);
    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i] = std::async(compare_proc, i);
    }

    for (size_t i = 0; i < compare_thread_num; i++) {
      f_compare[i].get();
    }

    // batch_result is scoped and will be destructed soon
    for (auto& r : batch_result) {
      indices.insert(indices.end(), std::make_move_iterator(r.begin()),
                     std::make_move_iterator(r.end()));
    }

    batch_count++;

    item_index += server_masked_items.size();
    SPDLOG_INFO("GetIndices batch count:{}, item_index:{}", batch_count,
                item_index);
  }

  return std::make_pair(indices, item_index);
}

void EcdhOprfPsiClient::RecvFinalEvaluatedItems(
    const std::shared_ptr<IEcPointStore>& peer_ec_point_store) {
  SPDLOG_INFO("Begin Recv FinalEvaluatedItems items");

  size_t batch_count = 0;
  while (true) {
    const auto tag =
        fmt::format("EcdhOprfPSI:FinalEvaluatedItems:{}", batch_count);

    // Fetch y^b.
    PsiDataBatch masked_batch =
        PsiDataBatch::Deserialize(options_.cache_transfer_link->Recv(
            options_.cache_transfer_link->NextRank(), tag));

    if (masked_batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}", __func__,
                  batch_count);
      break;
    }

    YACL_ENFORCE(masked_batch.flatten_bytes.length() % compare_length_ == 0);
    size_t num_items = masked_batch.flatten_bytes.length() / compare_length_;

    std::vector<std::string> evaluated_items(num_items);
    for (size_t idx = 0; idx < num_items; ++idx) {
      evaluated_items[idx] = masked_batch.flatten_bytes.substr(
          idx * compare_length_, compare_length_);
      SPDLOG_DEBUG("evaluated_items: [{}] {}", idx,
                   absl::Base64Escape(evaluated_items[idx]));
    }
    peer_ec_point_store->Save(evaluated_items, masked_batch.duplicate_item_cnt);

    batch_count++;
  }
  SPDLOG_INFO("End Recv FinalEvaluatedItems items");
}

void EcdhOprfPsiClient::SendServerCacheIndexes(
    const std::vector<uint32_t>& peer_indexes,
    const std::vector<uint32_t>& self_indexes) {
  SPDLOG_INFO("Start SendServerCacheIndexes");
  options_.online_link->SendAsyncThrottled(
      options_.online_link->NextRank(), utils::SerializeIndexes(peer_indexes),
      "cache indexes");
  options_.online_link->SendAsyncThrottled(
      options_.online_link->NextRank(), utils::SerializeIndexes(self_indexes),
      "client indexes");
  SPDLOG_INFO("End SendServerCacheIndexes, {}", peer_indexes.size());
}

size_t EcdhOprfPsiClient::SendBlindedItems(
    const std::shared_ptr<IBasicBatchProvider>& batch_provider,
    bool server_get_result) {
  size_t batch_count = 0;
  size_t items_count = 0;

  SPDLOG_INFO("Begin Send BlindedItems items");

  while (true) {
    std::vector<std::string> items;
    std::unordered_map<uint32_t, uint32_t> dup_cnt;
    std::tie(items, dup_cnt) = batch_provider->ReadNextBatchWithDupCnt();

    PsiDataBatch blinded_batch;
    blinded_batch.is_last_batch = items.empty();

    const auto tag = fmt::format("EcdhOprfPSI:BlindItems:{}", batch_count);

    if (blinded_batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}", __func__,
                  batch_count);

      options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                               blinded_batch.Serialize(), tag);
      break;
    }

    std::vector<std::shared_ptr<IEcdhOprfClient>> oprf_clients(items.size());
    std::vector<std::string> blinded_items(items.size());

    yacl::parallel_for(0, items.size(), [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        if (oprf_client_ == nullptr) {
          std::shared_ptr<IEcdhOprfClient> oprf_client_ptr =
              CreateEcdhOprfClient(options_.oprf_type, options_.curve_type);

          oprf_clients[idx] = oprf_client_ptr;
        } else {
          oprf_clients[idx] = oprf_client_;
        }

        blinded_items[idx] = oprf_clients[idx]->Blind(items[idx]);
      }
    });

    blinded_batch.flatten_bytes.reserve(items.size() * ec_point_length_);
    if (server_get_result) {
      blinded_batch.duplicate_item_cnt = dup_cnt;
    }

    for (uint64_t idx = 0; idx < items.size(); ++idx) {
      blinded_batch.flatten_bytes.append(blinded_items[idx]);
    }

    // push to oprf_client_queue_
    if (oprf_client_ == nullptr) {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_push_cv_.wait(lock, [&] {
        return (oprf_client_queue_.size() < options_.window_size);
      });
      oprf_client_queue_.push(std::move(oprf_clients));
      queue_pop_cv_.notify_one();
      SPDLOG_DEBUG("push to queue size:{}", oprf_client_queue_.size());
    }

    options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                             blinded_batch.Serialize(), tag);

    items_count += items.size();
    batch_count++;
  }
  SPDLOG_INFO("{} finished, batch_count={} items_count={}", __func__,
              batch_count, items_count);

  return items_count;
}

void EcdhOprfPsiClient::RecvEvaluatedItems(
    const std::shared_ptr<IEcPointStore>& self_ec_point_store) {
  SPDLOG_INFO("Begin Recv EvaluatedItems items");

  size_t batch_count = 0;
  size_t item_count = 0;

  while (true) {
    const auto tag = fmt::format("EcdhOprfPSI:EvaluatedItems:{}", batch_count);
    PsiDataBatch masked_batch = PsiDataBatch::Deserialize(
        options_.online_link->Recv(options_.online_link->NextRank(), tag));
    // Fetch evaluate y^rs.

    if (masked_batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}, item_count: {}",
                  __func__, batch_count, item_count);
      break;
    }

    YACL_ENFORCE(masked_batch.flatten_bytes.size() % ec_point_length_ == 0);
    size_t num_items = masked_batch.flatten_bytes.size() / ec_point_length_;
    item_count += num_items;

    std::vector<std::string> evaluate_items(num_items);
    for (size_t idx = 0; idx < num_items; ++idx) {
      evaluate_items[idx] = masked_batch.flatten_bytes.substr(
          idx * ec_point_length_, ec_point_length_);
    }

    std::vector<std::string> oprf_items(num_items);
    std::vector<std::shared_ptr<IEcdhOprfClient>> oprf_clients;

    // get oprf_clients
    if (oprf_client_ == nullptr) {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_pop_cv_.wait(lock, [&] { return (!oprf_client_queue_.empty()); });

      oprf_clients = std::move(oprf_client_queue_.front());
      oprf_client_queue_.pop();
      queue_push_cv_.notify_one();
    } else {
      oprf_clients.resize(num_items);
      for (size_t i = 0; i < oprf_clients.size(); ++i) {
        oprf_clients[i] = oprf_client_;
      }
    }

    YACL_ENFORCE(oprf_clients.size() == num_items,
                 "EcdhOprfServer should not be nullptr");

    yacl::parallel_for(0, num_items, [&](int64_t begin, int64_t end) {
      for (int64_t idx = begin; idx < end; ++idx) {
        oprf_items[idx] = oprf_clients[idx]->Finalize(evaluate_items[idx]);
        SPDLOG_DEBUG("oprf_item[{}]: {}", idx,
                     absl::Base64Escape(oprf_items[idx]));
      }
    });

    self_ec_point_store->Save(oprf_items, masked_batch.duplicate_item_cnt);

    batch_count++;
  }
  SPDLOG_INFO("End Recv EvaluatedItems");
}

void EcdhOprfPsiClient::SendIntersectionMaskedItems(
    const std::shared_ptr<IBasicBatchProvider>& batch_provider) {
  size_t batch_count = 0;
  size_t items_count = 0;

  SPDLOG_INFO("Begin Send IntersectionMaskedItems items");

  while (true) {
    auto items = batch_provider->ReadNextBatch();

    PsiDataBatch blinded_batch;
    blinded_batch.is_last_batch = items.empty();

    const auto tag = fmt::format("EcdhOprfPSI:batch_count:{}", batch_count);

    if (blinded_batch.is_last_batch) {
      SPDLOG_INFO("{} Last batch triggered, batch_count={}", __func__,
                  batch_count);

      options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                               blinded_batch.Serialize(), tag);
      break;
    }

    blinded_batch.flatten_bytes.reserve(items.size() * compare_length_);

    for (uint64_t idx = 0; idx < items.size(); ++idx) {
      std::string b64_dest;
      absl::Base64Unescape(items[idx], &b64_dest);

      blinded_batch.flatten_bytes.append(b64_dest);
    }

    options_.online_link->SendAsyncThrottled(options_.online_link->NextRank(),
                                             blinded_batch.Serialize(), tag);

    items_count += items.size();
    batch_count++;
  }
  SPDLOG_INFO("{} finished, batch_count={} items_count={}", __func__,
              batch_count, items_count);

  return;
}

}  // namespace psi::ecdh
