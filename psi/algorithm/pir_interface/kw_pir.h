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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yacl/base/byte_container_view.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/utils/cuckoo_index.h"

namespace psi::pir {

struct KwPirOptions {
  CuckooIndex::Options cuckoo_options_{1 << 16, 0, 3, 1.3};

  uint64_t key_size = 16;
  uint64_t value_size = 256;
};

class KwPir {
 public:
  using HashType = uint128_t;

  explicit KwPir(const KwPirOptions& options) : options_(options) {
    YACL_ENFORCE_EQ(options_.cuckoo_options_.num_stash, 0UL);
  }
  virtual ~KwPir() = default;

 protected:
  KwPirOptions options_;
};

class KwPirServer : public KwPir {
 public:
  explicit KwPirServer(const KwPirOptions& options,
                       std::unique_ptr<IndexPirServer> pir_server)
      : KwPir(options),
        cuckoo_index_(options.cuckoo_options_),
        pir_server_(std::move(pir_server)) {}
  ~KwPirServer() override = default;

  void SetDatabase(
      const std::vector<
          std::pair<yacl::ByteContainerView, yacl::ByteContainerView>>& db_vec);

  std::vector<yacl::Buffer> GenerateResponse(
      const std::vector<yacl::Buffer>& query_vec) const;

  yacl::Buffer GenerateResponse(const yacl::Buffer& query_vec) const;

 private:
  psi::CuckooIndex cuckoo_index_;
  std::unique_ptr<IndexPirServer> pir_server_;
};

class KwPirClient : public KwPir {
 public:
  explicit KwPirClient(const KwPirOptions& options,
                       std::unique_ptr<IndexPirClient> pir_client)
      : KwPir(options), pir_client_(std::move(pir_client)) {}
  ~KwPirClient() override = default;

  std::pair<std::vector<yacl::Buffer>, std::vector<uint64_t>> GenerateQuery(
      yacl::ByteContainerView keyword) const;

  std::vector<std::vector<uint8_t>> DecodeResponse(
      const std::vector<yacl::Buffer>& response,
      const std::vector<uint64_t>& raw_idxs) const;
  std::vector<uint8_t> DecodeResponse(const yacl::Buffer& response,
                                      uint64_t raw_idx) const;

 private:
  std::unique_ptr<IndexPirClient> pir_client_;
};
}  // namespace psi::pir
