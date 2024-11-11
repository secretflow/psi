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

namespace psi::kwpir {

class IndexPirServer {
 public:
  virtual ~IndexPirServer() = default;
  virtual void SetDatabase(
      const std::vector<yacl::ByteContainerView>& db_vec) = 0;
  virtual yacl::Buffer GenerateIndexReply(const yacl::Buffer& query_buffer) = 0;
};

class IndexPirClient {
 public:
  virtual ~IndexPirClient() = default;
  virtual yacl::Buffer GenerateIndexQuery(uint64_t ele_index,
                                          uint64_t& offset) = 0;
  virtual std::vector<uint8_t> DecodeIndexReply(
      const yacl::Buffer& reply_buffer, uint64_t offset) = 0;
};
}  // namespace psi::kwpir