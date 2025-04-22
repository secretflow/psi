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

#pragma once

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/util/delimited_message_util.h"
#include "yacl/base/byte_container_view.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/utils/cuckoo_index.h"

#include "psi/algorithm/pir_interface/pir_type.pb.h"

namespace psi::pir {

struct KwPirOptions {
  CuckooIndex::Options cuckoo_options_{1 << 16, 0, 3, 1.3};
  uint64_t value_size = 0;

  KwPirOptionsProto SerializeToProto() const;

  static KwPirOptions DeserializeFromProto(const KwPirOptionsProto& proto);
};

class KwPir {
 public:
  using HashType = uint128_t;

  static HashType HashStr(const std::string& data);

  static std::vector<HashType> HashStrVec(const std::vector<std::string>& data);

  explicit KwPir(const KwPirOptions& options) : options_(options) {
    YACL_ENFORCE_EQ(options_.cuckoo_options_.num_stash, 0UL);
  }
  virtual ~KwPir() = default;

 protected:
  KwPirOptions options_;
};

class KwPirServer : public KwPir, public KwPirDataBase {
 public:
  explicit KwPirServer(const KwPirOptions& options,
                       std::unique_ptr<IndexPirServer> pir_server)
      : KwPir(options),
        cuckoo_index_(options.cuckoo_options_),
        pir_server_(std::move(pir_server)) {}
  ~KwPirServer() override = default;

  void GenerateFromRawKeyData(
      const std::vector<yacl::ByteContainerView>& db_vec) override;

  // the input is a vector, which contains many key-value pairs
  // note that each key's length must be the same, and the ecah value's
  // length must be the same
  void GenerateFromRawKeyValueData(
      const std::vector<std::pair<yacl::ByteContainerView,
                                  yacl::ByteContainerView>>& db_vec) override;

  void GenerateFromRawKeyValueData(
      const std::vector<std::string>& keys,
      const std::vector<std::string>& values) override;

  void GenerateFromRawKeyValueData(
      const std::vector<uint128_t>& keys,
      const std::vector<std::string>& values) override;

  std::vector<yacl::Buffer> Response(const std::vector<yacl::Buffer>& query_vec,
                                     const yacl::Buffer& pks) const override;

  std::vector<std::string> Response(const std::vector<std::string>& query_vec,
                                    const std::string& pks) const override;

  yacl::Buffer Response(const yacl::Buffer& query_vec,
                        const yacl::Buffer& pks) const override;

  std::string Response(const std::string& query_vec,
                       const std::string& pks) const override;

  PirType GetPirType() const override { return pir_server_->GetPirType(); }

  void Dump(std::ostream& out_stream) const override;

  static std::unique_ptr<KwPirServer> Load(
      google::protobuf::io::FileInputStream& input);

 private:
  void GenerateFromRawKeyValueDataInternal(
      const std::vector<uint128_t>& keys,
      const std::vector<std::string>& values);

  psi::CuckooIndex cuckoo_index_;
  std::unique_ptr<IndexPirServer> pir_server_;
};

std::unique_ptr<KwPirServer> CreateKwPirServer(psi::pir::PirType pir_type,
                                               size_t value_size,
                                               size_t input_num);

class KwPirClient : public KwPir {
 public:
  struct QueryContext {
    std::string keyword;
    HashType key_hash;
    std::vector<std::string> query_vec;
    std::vector<uint64_t> raw_idxs;
    bool matched = false;
    std::string value;
  };

  explicit KwPirClient(const KwPirOptions& options,
                       std::unique_ptr<IndexPirClient> pir_client)
      : KwPir(options), pir_client_(std::move(pir_client)) {}
  ~KwPirClient() override = default;

  yacl::Buffer GeneratePksBuffer() const;
  std::string GeneratePksString() const;

  // first is encoded query, second is offset index which is used to decode
  // response but no need to send to server
  std::pair<std::vector<yacl::Buffer>, std::vector<uint64_t>> GenerateQuery(
      yacl::ByteContainerView keyword) const;
  std::pair<std::vector<std::string>, std::vector<uint64_t>> GenerateQueryStr(
      yacl::ByteContainerView keyword) const;

  size_t IndexQueryPerKeyword() const;

  std::vector<std::vector<uint8_t>> DecodeResponse(
      const std::vector<yacl::Buffer>& response,
      const std::vector<uint64_t>& raw_idxs) const;

  std::vector<std::vector<uint8_t>> DecodeResponse(
      const std::vector<std::string>& response,
      const std::vector<uint64_t>& raw_idxs) const;

  std::vector<uint8_t> DecodeResponse(const yacl::Buffer& response,
                                      uint64_t raw_idx) const;
  std::vector<uint8_t> DecodeResponse(const std::string& response,
                                      uint64_t raw_idx) const;

  // interface for context
  QueryContext GenerateQueryCtx(std::string keyword) const;
  bool DecodeResponse(QueryContext* ctx,
                      const std::vector<std::string>& response) const;

 private:
  std::unique_ptr<IndexPirClient> pir_client_;
};

std::unique_ptr<KwPirClient> CreateKwPirClient(psi::pir::PirType pir_type,
                                               size_t value_size,
                                               size_t input_num);

}  // namespace psi::pir
