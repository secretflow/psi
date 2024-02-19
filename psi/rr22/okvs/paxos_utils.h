// Copyright 2023 Ant Group Co., Ltd.
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
#include <memory>
#include <set>
#include <vector>

#include "absl/types/span.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22::okvs {

// An efficient data structure for tracking the weight of the
// paxos columns (node), which excludes the rows which have
// already been fixed (assigned to C).
template <typename IdxType>
struct WeightData {
  static constexpr IdxType NullNode = ~IdxType(0);

  struct WeightNode {
    // The current of this column.
    IdxType weight;

    // The previous column with the same weight.
    IdxType prev_weight_node = NullNode;

    // the next column with the same weight
    IdxType next_weight_node = NullNode;
  };

  std::vector<WeightNode*> weight_sets;

  std::vector<WeightNode> nodes;

  uint64_t node_alloc_size = 0;

  // returns the index of the node
  IdxType IdxOf(const WeightNode& node) {
    return static_cast<IdxType>(&node - nodes.data());
  }

  // returns the size of the set of weight w columns.
  uint64_t SetSize(uint64_t w) {
    uint64_t s = 0;
    auto cur = weight_sets[w];
    while (cur) {
      ++s;

      if (cur->next_weight_node != NullNode)
        cur = &nodes[cur->next_weight_node];
      else
        cur = nullptr;
    }

    return s;
  }

  // add the node/column to the data structure. Assumes
  // it is no already in it.
  void PushNode(WeightNode& node) {
    YACL_ENFORCE(node.next_weight_node == NullNode);
    YACL_ENFORCE(node.prev_weight_node == NullNode);

    if (weight_sets.size() <= node.weight) {
      weight_sets.resize(node.weight + 1, nullptr);
    }

    if (weight_sets[node.weight] == nullptr) {
      weight_sets[node.weight] = &node;
    } else {
      YACL_ENFORCE(weight_sets[node.weight]->prev_weight_node == NullNode);

      weight_sets[node.weight]->prev_weight_node = IdxOf(node);
      node.next_weight_node = IdxOf(*weight_sets[node.weight]);
      weight_sets[node.weight] = &node;
    }
  }

  // remove the given node/column from the data structure.
  void PopNode(WeightNode& node) {
    if (node.prev_weight_node == NullNode) {
      YACL_ENFORCE(weight_sets[node.weight] == &node);

      if (node.next_weight_node == NullNode) {
        weight_sets[node.weight] = nullptr;
        while (weight_sets.back() == nullptr) {
          weight_sets.pop_back();
        }
      } else {
        weight_sets[node.weight] = &nodes[node.next_weight_node];
        weight_sets[node.weight]->prev_weight_node = NullNode;
      }
    } else {
      auto& prev = nodes[node.prev_weight_node];

      if (node.next_weight_node == NullNode) {
        prev.next_weight_node = NullNode;
      } else {
        auto& next = nodes[node.next_weight_node];
        prev.next_weight_node = IdxOf(next);
        next.prev_weight_node = IdxOf(prev);
      }
    }

    node.prev_weight_node = NullNode;
    node.next_weight_node = NullNode;
  }

  // decrease the weight of a given node/column
  void DecementWeight(WeightNode& node) {
    YACL_ENFORCE(node.weight);

    PopNode(node);
    --node.weight;
    PushNode(node);
  }

  // returns the node with minimum weight.
  WeightNode& GetMinWeightNode() {
    for (size_t i = 1; i < weight_sets.size(); ++i) {
      if (weight_sets[i]) {
        auto& node = *weight_sets[i];
        return node;
      }
    }

    YACL_THROW("func:{} error {}", __func__, __LINE__);
  }

  // initialize the data structure with the current set of
  // node/column weights.
  void init(absl::Span<IdxType> weights) {
    SPDLOG_DEBUG("node_alloc_size:{} weights.size():{}", node_alloc_size,
                 weights.size());

    if (node_alloc_size < weights.size()) {
      node_alloc_size = weights.size();
      nodes.resize(node_alloc_size);

      SPDLOG_DEBUG("node_alloc_size:{} weights.size():{}", node_alloc_size,
                   weights.size());
    }

    weight_sets.clear();
    weight_sets.resize(200);
    // mNodes.resize(weights.size());

    for (IdxType i = 0; i < weights.size(); ++i) {
      nodes[i].weight = weights[i];

      auto& node = nodes[i];
      node.next_weight_node = NullNode;
      node.prev_weight_node = NullNode;

      YACL_ENFORCE(node.weight < weight_sets.size(),
                   "something went wrong, maybe duplicate inputs.");

      auto& ws = weight_sets[node.weight];
      if (!!(ws != nullptr)) {
        YACL_ENFORCE(ws->prev_weight_node == NullNode);

        ws->prev_weight_node = IdxOf(node);
        node.next_weight_node = IdxOf(*ws);
      }

      ws = &node;
    }

    for (uint64_t i = weight_sets.size() - 1; i < weight_sets.size(); --i) {
      if (weight_sets[i]) {
        weight_sets.resize(i + 1);
        break;
      }
    }
  }
};

// A Paxos vector type when the elements are of type T.
// This differs from PxMatrix which has elements that
// each a vector of type T's. PxVector are more efficient
// since we can remove an "inner for-loop."

struct PxVector {
  using value_type = uint128_t;
  using iterator = uint128_t*;
  using const_iterator = const uint128_t*;

  std::vector<value_type> owning;
  absl::Span<value_type> elements;

  PxVector() = default;
  PxVector(const PxVector& v) : PxVector(v.owning.size()) {
    std::memcpy(owning.data(), v.owning.data(),
                v.owning.size() * sizeof(value_type));
  }

  PxVector(PxVector&& v) : PxVector(v.owning.size()) {
    std::memcpy(owning.data(), v.owning.data(),
                v.owning.size() * sizeof(value_type));
  }

  PxVector(absl::Span<value_type> e) { elements = e; }

  PxVector(uint64_t size) {
    owning.resize(size);

    elements = absl::MakeSpan(owning.data(), owning.size());
  }

  // return a iterator to the i'th element. Should be pointer symmatics
  inline iterator operator[](uint64_t i) { return &elements[i]; }

  // return a iterator to the i'th element. Should be pointer symmatics
  inline const_iterator operator[](uint64_t i) const { return &elements[i]; }

  // return the size of the vector
  inline auto size() const { return elements.size(); }

  // return a subset of the vector, starting at index offset and of size count.
  // if count = -1, then get the rest of the vector.
  inline PxVector subspan(uint64_t offset, uint64_t count = -1) {
    return elements.subspan(offset, count);
  }

  // return a subset of the vector, starting at index offset and of size count.
  // if count = -1, then get the rest of the vector.
  inline PxVector subspan(uint64_t offset, uint64_t count = -1) const {
    return PxVector(elements.subspan(offset, count));
  }

  // populate the vector with the zero element.
  inline void ZeroFill() {
    // memset(mElements.data(), 0, mElements.size_bytes());
    memset(elements.data(), 0, elements.size() * sizeof(value_type));
  }

  // The default implementation of helper for PxVector.
  // This class performs operations of the elements of PxVector.
  struct Helper {
    // mutable version of value_type
    using mut_value_type = std::remove_const_t<value_type>;
    using mut_iterator = mut_value_type*;

    // internal mask used to multiply a value with a bit.
    // Assumes the zero bit string is the zero element.
    std::array<mut_value_type, 2> zeroOneMask;

    Helper() {
      memset(&zeroOneMask[0], 0, sizeof(value_type));
      memset(&zeroOneMask[1], -1, sizeof(value_type));
    }

    Helper(const Helper&) = default;

    // return a element that the user can use.
    inline static mut_value_type NewElement() { return {}; }

    // return the iterator for the return type of newElement().
    mut_iterator AsPtr(mut_value_type& t) { return &t; }

    // return a vector of elements that the user can use.
    inline static PxVector NewVec(uint64_t size) { return {size}; }

    // assign the src to the dst, ie *dst = *src.
    inline static void Assign(mut_iterator dst, const_iterator src) {
      *dst = *src;
    }

    // add the src to the dst, ie *dst += *src.
    inline static void Add(mut_iterator dst, const_iterator src1) {
      *dst = *dst ^ *src1;
    }

    // multiply src1 with m and add the result to the dst, ie *dst += (*src) *
    // m.
    inline static void MultAdd(mut_iterator dst, const_iterator src1,
                               const uint128_t& m) {
      *dst = *dst ^ (Galois128(*src1) * m).get<uint128_t>(0);
    }

    // multiply src1 with bit and add the result to the dst, ie *dst += (*src) *
    // bit.
    inline void MultAdd(mut_iterator dst, const_iterator src1,
                        const uint8_t& bit) {
      YACL_ENFORCE(bit < 2, "bit:{}", bit);
      *dst = *dst ^ (*src1 & zeroOneMask[bit]);
    }

    // return the iterator plus the given number of rows
    inline static auto IterPlus(const_iterator p, uint64_t i) { return p + i; }

    // return the iterator plus the given number of rows
    inline static auto IterPlus(mut_iterator p, uint64_t i) { return p + i; }

    // randomize the given element.
    inline static void Randomize(
        mut_iterator p,
        const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng) {
      prng->Fill(absl::MakeSpan(p, 1));
    }

    inline static auto eq(iterator p0, iterator p1) { return *p0 == *p1; }
  };

  // return the default helper for this vector type.
  static Helper DefaultHelper() { return {}; }
};

}  // namespace psi::rr22::okvs
