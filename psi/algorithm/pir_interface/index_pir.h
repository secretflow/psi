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

#include <vector>

#include "yacl/base/byte_container_view.h"

#include "psi/algorithm/pir_interface/pir_db.h"

namespace psi::pir {

class IndexPirClient {
 public:
  virtual ~IndexPirClient() = default;

  virtual yacl::Buffer GenerateIndexQuery(uint64_t raw_idx) const = 0;

  virtual std::vector<uint8_t> DecodeIndexResponse(
      const yacl::Buffer& response_buffer, uint64_t raw_idx) const = 0;
};

class IndexPirServer {
 public:
  virtual ~IndexPirServer() = default;
  virtual void SetDatabase(const psi::pir_utils::RawDatabase& raw_database) = 0;
  virtual yacl::Buffer GenerateIndexResponse(
      const yacl::Buffer& query_buffer) const = 0;
};

}  // namespace psi::pir
