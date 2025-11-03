// Copyright 2025 Ant Group Co., Ltd.
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

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "yacl/base/int128.h"

#include "psi/algorithm/types.h"

namespace psi {

struct PsiResultIndex {
  ItemIndexType data;
  ItemCntType peer_item_cnt = 1;
};

struct PsiItemData {
  std::string buf;
  ItemCntType cnt = 1;
};

struct PsiItemHash {
  HashType data;
  ItemCntType cnt = 1;
};

class IResultReceiver {
 public:
  virtual void Add(PsiResultIndex index) = 0;
  virtual void Add(std::vector<PsiResultIndex> indices) = 0;
  virtual void Finish() = 0;
};

class IResultStore {
 public:
  [[nodiscard]] virtual size_t GetBucketNum() const = 0;
  virtual std::shared_ptr<IResultReceiver> GetReceiver(size_t tag) = 0;
};

class IDataProvider {
 public:
  virtual std::vector<PsiItemData> ReadNext(size_t size) = 0;
  virtual std::vector<PsiItemData> ReadAll() = 0;
  [[nodiscard]] virtual size_t Size() const = 0;
};

class IDataStore {
 public:
  [[nodiscard]] virtual size_t GetBucketNum() const = 0;
  [[nodiscard]] virtual size_t GetBucketDatasize(size_t tag) const = 0;
  virtual std::shared_ptr<IDataProvider> Load(size_t tag) = 0;
};

}  // namespace psi
