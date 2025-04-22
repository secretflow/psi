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

#include "psi/algorithm/pir_interface/kw_pir.h"

#include "absl/strings/escaping.h"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/pir_interface/kw_pir.h"
#include "psi/algorithm/sealpir/seal_pir.h"
#include "psi/algorithm/spiral/spiral_server.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::pir {

namespace {

constexpr uint64_t kValueCountAdjustBase = 256;
constexpr uint64_t kValueCountAdjustPad = 256;

template <typename T>
void UniqueCheck(const std::vector<T>& vec) {
  std::set<T> data;
  for (const auto& item : vec) {
    YACL_ENFORCE(data.insert(item).second, "Duplicate data: {}", item);
  }
}

}  // namespace

KwPir::HashType KwPir::HashStr(const std::string& data) {
  return yacl::crypto::Blake3_128(data);
}

std::vector<KwPir::HashType> KwPir::HashStrVec(
    const std::vector<std::string>& data) {
  std::vector<uint128_t> res(data.size());
  yacl::parallel_for(0, data.size(), [&](uint64_t beg, uint64_t end) {
    for (uint64_t i = beg; i < end; i++) {
      res[i] = yacl::crypto::Blake3_128(data[i]);
    }
  });
  return res;
}

KwPirOptionsProto KwPirOptions::SerializeToProto() const {
  KwPirOptionsProto proto;

  proto.mutable_cuckoo_options()->set_num_input(cuckoo_options_.num_input);
  proto.mutable_cuckoo_options()->set_num_stash(cuckoo_options_.num_stash);
  proto.mutable_cuckoo_options()->set_num_hash(cuckoo_options_.num_hash);
  proto.mutable_cuckoo_options()->set_scale_factor(
      cuckoo_options_.scale_factor);
  proto.mutable_cuckoo_options()->set_max_try_count(
      cuckoo_options_.max_try_count);
  proto.set_value_size(value_size);

  return proto;
}

KwPirOptions KwPirOptions::DeserializeFromProto(
    const KwPirOptionsProto& proto) {
  size_t num_input = proto.cuckoo_options().num_input();
  size_t num_stash = proto.cuckoo_options().num_stash();
  size_t num_hash = proto.cuckoo_options().num_hash();
  double scale_factor = proto.cuckoo_options().scale_factor();
  size_t max_try_count = proto.cuckoo_options().max_try_count();

  CuckooIndex::Options cuckoo_options{num_input, num_stash, num_hash,
                                      scale_factor, max_try_count};

  size_t value_size = proto.value_size();

  return KwPirOptions{cuckoo_options, value_size};
}

void KwPirServer::Dump(std::ostream& out_stream) const {
  YACL_ENFORCE(out_stream, "Output stream is not in a good state for writing.");
  YACL_ENFORCE(pir_server_->DbSeted(), "Before dump, database must be seted.");

  // write KwOpions
  auto kw_option_proto = options_.SerializeToProto();
  google::protobuf::util::SerializeDelimitedToOstream(kw_option_proto,
                                                      &out_stream);
  // write type
  auto pir_type_proto = PirTypeToProto(GetPirType());
  google::protobuf::util::SerializeDelimitedToOstream(pir_type_proto,
                                                      &out_stream);
  // write server
  pir_server_->Dump(out_stream);
}

std::unique_ptr<KwPirServer> KwPirServer::Load(
    google::protobuf::io::FileInputStream& input) {
  // read KwOpions
  KwPirOptionsProto option_proto;
  google::protobuf::util::ParseDelimitedFromZeroCopyStream(&option_proto,
                                                           &input, nullptr);
  auto options = KwPirOptions::DeserializeFromProto(option_proto);

  // read type
  pir::PirTypeProto pir_type_proto;
  google::protobuf::util::ParseDelimitedFromZeroCopyStream(&pir_type_proto,
                                                           &input, nullptr);
  auto type = psi::pir::ProtoToPirType(pir_type_proto);

  // load index server
  std::unique_ptr<IndexPirServer> pir_server{nullptr};
  switch (type) {
    case PirType::SEAL_PIR:
      pir_server = sealpir::SealPirServer::Load(input);
      break;
    case PirType::SPIRAL_PIR:
      pir_server = spiral::SpiralServer::Load(input);
      break;
    default:
      throw std::invalid_argument("pir type is invalid");
  }

  return std::make_unique<KwPirServer>(options, std::move(pir_server));
}

void KwPirServer::GenerateFromRawKeyData(
    const std::vector<yacl::ByteContainerView>& db_vec) {
  uint64_t num_input = options_.cuckoo_options_.num_input;
  YACL_ENFORCE_EQ(db_vec.size(), num_input);
  // there is no payload, so the value size is zero.
  YACL_ENFORCE_EQ(options_.value_size, 0U);

  std::vector<HashType> hash_vec;
  hash_vec.reserve(num_input);
  for (uint64_t i = 0; i < num_input; ++i) {
    YACL_ENFORCE_GT(db_vec[i].size(), 0U);
    hash_vec.push_back(yacl::crypto::Blake3_128(db_vec[i]));
  }
  cuckoo_index_.Insert(absl::Span<const HashType>(hash_vec));

  uint64_t item_size = sizeof(HashType);
  std::vector<CuckooIndex::Bin> bins = cuckoo_index_.bins();
  // index -> entry
  std::vector<std::vector<uint8_t>> index_db_vec_store;
  index_db_vec_store.reserve(bins.size());
  for (uint64_t i = 0; i < bins.size(); ++i) {
    std::vector<uint8_t> item(item_size, static_cast<uint8_t>(-1));

    if (!bins[i].IsEmpty()) {
      uint64_t raw_index = bins[i].InputIdx();
      std::memcpy(item.data(), &hash_vec[raw_index], sizeof(HashType));
    }
    index_db_vec_store.emplace_back(std::move(item));
  }
  YACL_ENFORCE_EQ(index_db_vec_store.size(), bins.size());

  RawDatabase raw_db(index_db_vec_store);

  pir_server_->GenerateFromRawData(raw_db);
}

void KwPirServer::GenerateFromRawKeyValueData(
    const std::vector<std::string>& keys,
    const std::vector<std::string>& values) {
  uint64_t num_input = options_.cuckoo_options_.num_input;
  YACL_ENFORCE_EQ(keys.size(), num_input);
  YACL_ENFORCE_EQ(values.size(), num_input);

  UniqueCheck(keys);

  std::vector<HashType> hash_vec(num_input);
  // TODO: maybe parallel execute
  for (size_t i = 0; i < num_input; ++i) {
    YACL_ENFORCE_GT(keys[i].size(), 0U);

    hash_vec[i] = yacl::crypto::Blake3_128(keys[i]);
  }
  GenerateFromRawKeyValueDataInternal(hash_vec, values);
}

void KwPirServer::GenerateFromRawKeyValueDataInternal(
    const std::vector<uint128_t>& keys,
    const std::vector<std::string>& values) {
  // TODO: maybe parallel execute
  for (size_t i = 0; i < options_.cuckoo_options_.num_input; ++i) {
    YACL_ENFORCE_EQ(values[i].size(), options_.value_size);
  }

  cuckoo_index_.Insert(absl::Span<const HashType>(keys));

  uint64_t item_size = sizeof(HashType) + options_.value_size;
  std::vector<CuckooIndex::Bin> bins = cuckoo_index_.bins();
  std::vector<std::vector<uint8_t>> index_db_vec_store;
  index_db_vec_store.reserve(bins.size());
  for (uint64_t i = 0; i < bins.size(); ++i) {
    std::vector<uint8_t> item(item_size, static_cast<uint8_t>(-1));

    if (!bins[i].IsEmpty()) {
      uint64_t raw_index = bins[i].InputIdx();

      memcpy(item.data(), &keys[raw_index], sizeof(HashType));
      memcpy(item.data() + sizeof(HashType), values[raw_index].c_str(),
             options_.value_size);
    }
    index_db_vec_store.emplace_back(std::move(item));
  }
  YACL_ENFORCE_EQ(index_db_vec_store.size(), bins.size());

  RawDatabase raw_db(index_db_vec_store);

  pir_server_->GenerateFromRawData(raw_db);
}

void KwPirServer::GenerateFromRawKeyValueData(
    const std::vector<uint128_t>& keys,
    const std::vector<std::string>& values) {
  uint64_t num_input = options_.cuckoo_options_.num_input;
  YACL_ENFORCE_EQ(keys.size(), num_input);
  YACL_ENFORCE_EQ(values.size(), num_input);

  UniqueCheck(keys);

  GenerateFromRawKeyValueDataInternal(keys, values);
}

void KwPirServer::GenerateFromRawKeyValueData(
    const std::vector<
        std::pair<yacl::ByteContainerView, yacl::ByteContainerView>>& db_vec) {
  uint64_t num_input = options_.cuckoo_options_.num_input;
  YACL_ENFORCE_EQ(db_vec.size(), num_input);

  std::vector<uint128_t> keys;
  keys.reserve(num_input);
  std::vector<std::string> values;
  values.reserve(num_input);

  // TODO: maybe parallel execute
  for (uint64_t i = 0; i < num_input; ++i) {
    YACL_ENFORCE_GT(db_vec[i].first.size(), 0U);
    YACL_ENFORCE_EQ(db_vec[i].second.size(), options_.value_size);

    keys.emplace_back(yacl::crypto::Blake3_128(db_vec[i].first));
    values.emplace_back(db_vec[i].second.begin(), db_vec[i].second.end());
  }

  UniqueCheck(keys);

  GenerateFromRawKeyValueDataInternal(keys, values);
}

std::vector<std::string> KwPirServer::Response(
    const std::vector<std::string>& query, const std::string& pks) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(query.size(), num_hash);

  std::vector<std::string> reply_vec;
  reply_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    SPDLOG_INFO("Doing {}-th query.", hash_index);
    auto reply = pir_server_->Response(query[hash_index], pks);
    reply_vec.emplace_back(std::move(reply));
  }
  return reply_vec;
}

std::vector<yacl::Buffer> KwPirServer::Response(
    const std::vector<yacl::Buffer>& query, const yacl::Buffer& pks) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(query.size(), num_hash);

  std::vector<yacl::Buffer> reply_vec;
  reply_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    yacl::Buffer reply = pir_server_->Response(query[hash_index], pks);
    reply_vec.emplace_back(std::move(reply));
  }
  return reply_vec;
}

yacl::Buffer KwPirServer::Response(const yacl::Buffer& query,
                                   const yacl::Buffer& pks) const {
  yacl::Buffer reply = pir_server_->Response(query, pks);
  return reply;
}
std::string KwPirServer::Response(const std::string& query,
                                  const std::string& pks) const {
  return pir_server_->Response(query, pks);
}

yacl::Buffer KwPirClient::GeneratePksBuffer() const {
  return pir_client_->GeneratePksBuffer();
}

std::string KwPirClient::GeneratePksString() const {
  return pir_client_->GeneratePksString();
}

size_t KwPirClient::IndexQueryPerKeyword() const {
  return options_.cuckoo_options_.num_hash;
}

KwPirClient::QueryContext KwPirClient::GenerateQueryCtx(
    std::string keyword) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  QueryContext ctx;

  ctx.raw_idxs.reserve(num_hash);
  ctx.query_vec.reserve(num_hash);
  ctx.key_hash = yacl::crypto::Blake3_128(keyword);
  ctx.keyword = std::move(keyword);

  CuckooIndex::HashRoom hash_room(ctx.key_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    uint64_t ele_index =
        hash_room.GetHash(hash_index) % options_.cuckoo_options_.NumBins();
    ctx.raw_idxs.push_back(ele_index);

    auto query = pir_client_->GenerateIndexQueryStr(ele_index);

    ctx.query_vec.emplace_back(std::move(query));
  }

  return ctx;
}

bool KwPirClient::DecodeResponse(
    QueryContext* ctx, const std::vector<std::string>& response) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(response.size(), num_hash);
  YACL_ENFORCE_EQ(ctx->raw_idxs.size(), num_hash);

  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    std::vector<uint8_t> ele = pir_client_->DecodeIndexResponse(
        response[hash_index], ctx->raw_idxs[hash_index]);
    YACL_ENFORCE(ele.size() >= sizeof(HashType),
                 "ele size is too small, ele size: {}, key hash: {}",
                 ele.size(), sizeof(ctx->key_hash));
    if (memcmp(&ctx->key_hash, ele.data(), sizeof(HashType)) != 0) {
      continue;
    }

    ctx->value.assign(reinterpret_cast<char*>(ele.data() + sizeof(HashType)),
                      ele.size() - sizeof(HashType));
    ctx->matched = true;
    break;
  }
  return ctx->matched;
}

std::pair<std::vector<yacl::Buffer>, std::vector<uint64_t>>
KwPirClient::GenerateQuery(yacl::ByteContainerView keyword) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;

  std::vector<uint64_t> idx_vec;
  idx_vec.reserve(num_hash);

  std::vector<yacl::Buffer> query_vec;
  query_vec.reserve(num_hash);
  HashType key_hash = yacl::crypto::Blake3_128(keyword);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    CuckooIndex::HashRoom hash_room(key_hash);
    uint64_t ele_index =
        hash_room.GetHash(hash_index) % options_.cuckoo_options_.NumBins();
    idx_vec.push_back(ele_index);

    auto query = pir_client_->GenerateIndexQuery(ele_index);

    query_vec.emplace_back(std::move(query));
  }

  return std::make_pair(query_vec, idx_vec);
}
std::pair<std::vector<std::string>, std::vector<uint64_t>>
KwPirClient::GenerateQueryStr(yacl::ByteContainerView keyword) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;

  std::vector<uint64_t> idx_vec;
  idx_vec.reserve(num_hash);

  std::vector<std::string> query_vec;
  query_vec.reserve(num_hash);
  HashType key_hash = yacl::crypto::Blake3_128(keyword);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    CuckooIndex::HashRoom hash_room(key_hash);
    uint64_t ele_index =
        hash_room.GetHash(hash_index) % options_.cuckoo_options_.NumBins();
    idx_vec.push_back(ele_index);

    auto query = pir_client_->GenerateIndexQueryStr(ele_index);

    query_vec.emplace_back(std::move(query));
  }

  return std::make_pair(query_vec, idx_vec);
}

std::vector<uint8_t> KwPirClient::DecodeResponse(const yacl::Buffer& response,
                                                 uint64_t raw_idx) const {
  std::vector<uint8_t> ele =
      pir_client_->DecodeIndexResponse(response, raw_idx);
  return ele;
}
std::vector<uint8_t> KwPirClient::DecodeResponse(const std::string& response,
                                                 uint64_t raw_idx) const {
  std::vector<uint8_t> ele =
      pir_client_->DecodeIndexResponse(response, raw_idx);
  return ele;
}

std::vector<std::vector<uint8_t>> KwPirClient::DecodeResponse(
    const std::vector<yacl::Buffer>& response,
    const std::vector<uint64_t>& raw_idxs) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(response.size(), num_hash);
  YACL_ENFORCE_EQ(raw_idxs.size(), num_hash);

  std::vector<std::vector<uint8_t>> ele_vec;
  ele_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    std::vector<uint8_t> ele = pir_client_->DecodeIndexResponse(
        response[hash_index], raw_idxs[hash_index]);
    ele_vec.emplace_back(std::move(ele));
  }
  return ele_vec;
}

std::vector<std::vector<uint8_t>> KwPirClient::DecodeResponse(
    const std::vector<std::string>& response,
    const std::vector<uint64_t>& raw_idxs) const {
  uint64_t num_hash = options_.cuckoo_options_.num_hash;
  YACL_ENFORCE_EQ(response.size(), num_hash);
  YACL_ENFORCE_EQ(raw_idxs.size(), num_hash);

  std::vector<std::vector<uint8_t>> ele_vec;
  ele_vec.reserve(num_hash);
  for (uint64_t hash_index = 0; hash_index < num_hash; ++hash_index) {
    std::vector<uint8_t> ele = pir_client_->DecodeIndexResponse(
        response[hash_index], raw_idxs[hash_index]);
    ele_vec.emplace_back(std::move(ele));
  }
  return ele_vec;
}

uint64_t AdjustIndexPirInputNum(uint64_t input_num, double scale_factor) {
  auto adjust = input_num * scale_factor;
  adjust =
      adjust > kValueCountAdjustBase ? adjust : (adjust + kValueCountAdjustPad);
  return adjust;
}

psi::sealpir::SealPirOptions MakeSealOptions(const KwPirOptions& options) {
  psi::sealpir::SealPirOptions seal_options;
  // element_number must be equal CuckooHashBins
  seal_options.element_number = options.cuckoo_options_.NumBins();
  seal_options.element_size =
      sizeof(psi::pir::KwPir::HashType) + options.value_size;
  return seal_options;
}

std::unique_ptr<KwPirServer> CreateKwPirServer(psi::pir::PirType pir_type,
                                               size_t value_size,
                                               size_t input_num) {
  KwPirOptions options;
  std::unique_ptr<IndexPirServer> index_pir_server;
  switch (pir_type) {
    case PirType::SEAL_PIR: {
      options.cuckoo_options_.num_input = input_num;
      options.value_size = value_size;

      psi::sealpir::SealPirOptions seal_options = MakeSealOptions(options);
      index_pir_server =
          std::make_unique<psi::sealpir::SealPirServer>(seal_options);
      break;
    }
    case PirType::SPIRAL_PIR: {
      // set cuckoo options
      options.cuckoo_options_.num_input = input_num;
      options.value_size = value_size;
      // set meta info
      spiral::DatabaseMetaInfo meta_info(
          options.cuckoo_options_.NumBins(),
          sizeof(psi::pir::KwPir::HashType) + value_size);
      // init and update prams
      auto spiral_params = spiral::util::GetDefaultParam();
      spiral_params.UpdateByDatabaseInfo(meta_info);
      // init a spiral server
      index_pir_server =
          std::make_unique<spiral::SpiralServer>(spiral_params, meta_info);
      break;
    }
    default:
      YACL_THROW("not support this type of PIR, type: {} ",
                 static_cast<int>(pir_type));
  }
  return std::make_unique<KwPirServer>(options, std::move(index_pir_server));
}

std::unique_ptr<KwPirClient> CreateKwPirClient(psi::pir::PirType pir_type,
                                               size_t value_size,
                                               size_t input_num) {
  KwPirOptions options;
  std::unique_ptr<IndexPirClient> index_pir_client;

  switch (pir_type) {
    case PirType::SEAL_PIR: {
      options.cuckoo_options_.num_input = input_num;
      options.value_size = value_size;

      psi::sealpir::SealPirOptions seal_options = MakeSealOptions(options);
      index_pir_client =
          std::make_unique<psi::sealpir::SealPirClient>(seal_options);
      break;
    }
    case PirType::SPIRAL_PIR: {
      // set cuckoo options
      options.cuckoo_options_.num_input = input_num;
      options.value_size = value_size;
      // set meta info
      spiral::DatabaseMetaInfo meta_info(
          options.cuckoo_options_.NumBins(),
          sizeof(psi::pir::KwPir::HashType) + value_size);
      // init and update prams
      auto spiral_params = spiral::util::GetDefaultParam();
      spiral_params.UpdateByDatabaseInfo(meta_info);
      // init a spiral client
      index_pir_client =
          std::make_unique<spiral::SpiralClient>(spiral_params, meta_info);
      break;
    }
    default:
      YACL_THROW("not support this type of PIR, type: {} ",
                 static_cast<int>(pir_type));
  }

  return std::make_unique<KwPirClient>(options, std::move(index_pir_client));
}

}  // namespace psi::pir
