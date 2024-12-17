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

#include "psi/algorithm/kwpir/kw_pir.h"

#include "yacl/crypto/hash/hash_utils.h"

namespace psi::kwpir {

void KwPirServer::SetDatabase(
    const std::vector<
        std::pair<yacl::ByteContainerView, yacl::ByteContainerView>>& db_vec) {
  uint64_t num_input = options_.cuckoo_options_.num_input;
  YACL_ENFORCE_EQ(db_vec.size(), num_input);

  std::vector<HashType> hash_vec(num_input);
  for (uint64_t i = 0; i < num_input; ++i) {
    YACL_ENFORCE_EQ(db_vec[i].first.size(), options_.key_size);
    YACL_ENFORCE_EQ(db_vec[i].second.size(), options_.value_size);

    HashType key_hash = yacl::crypto::Blake3_128(db_vec[i].first);
    hash_vec[i] = key_hash;
  }
  cuckoo_index_.Insert(absl::Span<const HashType>(hash_vec));

  uint64_t item_size = options_.key_size + options_.value_size;
  std::vector<CuckooIndex::Bin> bins = cuckoo_index_.bins();
  std::vector<std::vector<uint8_t>> index_db_vec_store;
  index_db_vec_store.reserve(bins.size());
  for (uint64_t i = 0; i < bins.size(); ++i) {
    std::vector<uint8_t> item(item_size);

    if (bins[i].IsEmpty()) {
      // empty value
      for (uint64_t j = 0; j < item_size; ++j) {
        item[j] = static_cast<uint8_t>(-1);
      }
    } else {
      uint64_t raw_index = bins[i].InputIdx();
      auto raw_data = db_vec[raw_index];

      memcpy(item.data(), raw_data.first.begin(), options_.key_size);
      memcpy(item.data() + options_.key_size, raw_data.second.begin(),
             options_.value_size);
    }
    index_db_vec_store.emplace_back(std::move(item));
  }
  YACL_ENFORCE_EQ(index_db_vec_store.size(), bins.size());

  std::vector<yacl::ByteContainerView> index_db_vec;
  index_db_vec.reserve(bins.size());
  for (uint64_t i = 0; i < bins.size(); ++i) {
    index_db_vec.emplace_back(index_db_vec_store[i]);
  }
  pir_server_->SetDatabase(index_db_vec);
}

std::vector<yacl::Buffer> KwPirServer::GenerateReply(
    const std::vector<yacl::Buffer>& query) {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(query.size(), num_hash);

  std::vector<yacl::Buffer> reply_vec;
  reply_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    yacl::Buffer reply = pir_server_->GenerateIndexReply(query[hash_index]);
    reply_vec.emplace_back(std::move(reply));
  }
  return reply_vec;
}

yacl::Buffer KwPirServer::GenerateReply(const yacl::Buffer& query) {
  yacl::Buffer reply = pir_server_->GenerateIndexReply(query);
  return reply;
}

std::vector<yacl::Buffer> KwPirClient::GenerateQuery(
    yacl::ByteContainerView keyword, std::vector<uint64_t>& offset) {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  offset.clear();
  offset.resize(num_hash);

  std::vector<yacl::Buffer> query_vec;
  query_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    HashType key_hash = yacl::crypto::Blake3_128(keyword);

    CuckooIndex::HashRoom hash_room(key_hash);
    uint64_t ele_index =
        hash_room.GetHash(hash_index) % options_.cuckoo_options_.NumBins();

    yacl::Buffer query =
        pir_client_->GenerateIndexQuery(ele_index, offset[hash_index]);

    query_vec.emplace_back(std::move(query));
  }

  return query_vec;
}

std::vector<uint8_t> KwPirClient::DecodeReply(const yacl::Buffer& reply,
                                              uint64_t offset) {
  std::vector<uint8_t> ele = pir_client_->DecodeIndexReply(reply, offset);
  return ele;
}

std::vector<std::vector<uint8_t>> KwPirClient::DecodeReply(
    const std::vector<yacl::Buffer>& reply,
    const std::vector<uint64_t>& offset) {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(reply.size(), num_hash);
  YACL_ENFORCE_EQ(offset.size(), num_hash);

  std::vector<std::vector<uint8_t>> ele_vec;
  ele_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    std::vector<uint8_t> ele =
        pir_client_->DecodeIndexReply(reply[hash_index], offset[hash_index]);
    ele_vec.emplace_back(std::move(ele));
  }
  return ele_vec;
}
}  // namespace psi::kwpir
