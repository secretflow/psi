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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "yacl/base/buffer.h"

#include "psi/utils/serializable.pb.h"

namespace psi::utils {

inline yacl::Buffer SerializeSize(size_t size) {
  proto::SizeProto proto;
  proto.set_input_size(size);
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline size_t DeserializeSize(const yacl::Buffer& buf) {
  proto::SizeProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  return proto.input_size();
}

inline yacl::Buffer SerializeIndexes(const std::vector<uint32_t>& index) {
  proto::IndexesProto proto;
  proto.mutable_indexes()->Assign(index.begin(), index.end());
  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::vector<uint32_t> DeserializeIndexes(const yacl::Buffer& buf) {
  proto::IndexesProto proto;
  proto.ParseFromArray(buf.data(), buf.size());
  std::vector<uint32_t> size(proto.indexes_size());
  std::copy(proto.indexes().begin(), proto.indexes().end(), size.begin());
  return size;
}

inline yacl::Buffer SerializeStrItems(
    const std::vector<std::string>& items,
    const std::unordered_map<uint32_t, uint32_t>& duplicate_item_cnt = {}) {
  proto::StrItemsProtoWithCnt proto;
  for (const auto& item : items) {
    proto.add_items(item);
  }
  for (auto& [k, v] : duplicate_item_cnt) {
    proto.mutable_duplicate_item_cnt()->insert({k, v});
  }

  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline void DeserializeStrItems(
    const yacl::Buffer& buf, std::vector<std::string>* items,
    std::unordered_map<uint32_t, uint32_t>* duplicate_item_cnt = nullptr) {
  proto::StrItemsProtoWithCnt proto;
  proto.ParseFromArray(buf.data(), buf.size());
  items->reserve(proto.items_size());
  for (auto item : proto.items()) {
    items->emplace_back(item);
  }
  if (duplicate_item_cnt == nullptr) {
    return;
  }
  auto& duplicate_cnt = *duplicate_item_cnt;
  for (auto& [k, v] : *proto.mutable_duplicate_item_cnt()) {
    duplicate_cnt.insert({k, v});
  }
}

inline yacl::Buffer SerializeItemsCnt(
    const std::unordered_map<uint32_t, uint32_t>& duplicate_item_cnt) {
  proto::ItemsCntProto proto;

  for (auto& [k, v] : duplicate_item_cnt) {
    proto.mutable_duplicate_item_cnt()->insert({k, v});
  }

  yacl::Buffer buf(proto.ByteSizeLong());
  proto.SerializeToArray(buf.data(), buf.size());
  return buf;
}

inline std::unordered_map<uint32_t, uint32_t> DeserializeItemsCnt(
    const yacl::Buffer& buf) {
  std::unordered_map<uint32_t, uint32_t> duplicate_item_cnt;
  proto::ItemsCntProto proto;
  proto.ParseFromArray(buf.data(), buf.size());

  for (auto& [k, v] : *proto.mutable_duplicate_item_cnt()) {
    duplicate_item_cnt[k] = v;
  }
  return duplicate_item_cnt;
}

inline size_t GetCompareBytesLength(size_t size_a, size_t size_b,
                                    size_t stats_params = 40) {
  size_t compare_bits = std::ceil(std::log2(size_a)) +
                        std::ceil(std::log2(size_b)) + stats_params;

  return (compare_bits + 7) / 8;
}

}  // namespace psi::utils
