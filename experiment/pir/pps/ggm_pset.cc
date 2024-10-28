#include "ggm_pset.h"
#include <spdlog/spdlog.h>

namespace pir::pps {

void GGMTree::AESPRG(uint128_t seed, uint128_t& left, uint128_t& right) {
  std::vector<uint128_t> r = yacl::crypto::PrgAesCtr<uint128_t>(seed, 2);
  left = r[0];
  right = r[1];
}

void GGMTree::Gen(std::vector<uint128_t>& leaf_nodes) {
  leaf_nodes.resize(1 << height_);
  leaf_nodes[0] = root_;
  std::vector<uint128_t> temp((1 << height_));
  for (uint32_t depth = 0; depth < height_; ++depth) {
    std::copy(leaf_nodes.begin(), leaf_nodes.begin() + (1 << depth),
              temp.begin());
    for (uint32_t i = 0; i < (1u << depth); ++i) {
      AESPRG(temp[i], leaf_nodes[2 * i], leaf_nodes[2 * i + 1]);
    }
  }
}

PIRKey PPS::Gen(uint32_t lambda) {
  PIRKey k;

  for (uint32_t cnt = 0; cnt < lambda; ++cnt) {
    k = yacl::crypto::RandU128();
    EvalMap(k);
    if (map_.size() == set_size_) {
      return k;
    }
  }

  SPDLOG_ERROR("Running lambda iterations of the loop unsuccessfully!");
  abort();
}

void PPS::Punc(uint64_t i, PIRKey k, PIRPuncKey& sk_punc) {
  auto iter = map_.find(i);
  if (iter != map_.end()) {
    uint32_t height = Depth(set_size_);
    sk_punc.pos_ = iter->second;
    yacl::dynamic_bitset<> bits(height, sk_punc.pos_);
    uint128_t root = k, left_child, right_child;
    for (uint32_t i = 0; i < height; ++i) {
      GGMTree::AESPRG(root, left_child, right_child);
      if (bits[height - 1 - i]) {
        sk_punc.k_.push_back(left_child);
        root = right_child;
      } else {
        sk_punc.k_.push_back(right_child);
        root = left_child;
      }
    }
  } else {
    SPDLOG_ERROR("i is not in pseudorandom sets ");
    abort();
  }
}

void PPS::EvalMap(PIRKey k) {
  uint32_t height = Depth(set_size_);
  GGMTree rand(k, height);
  std::vector<uint128_t> leaf_nodes;
  rand.Gen(leaf_nodes);

  if (!map_.empty()) {
    map_.clear();
  }
  for (uint64_t i = 0; i < set_size_; i++) {
    map_.emplace(LemireTrick(leaf_nodes[i], universe_size_), i);
  }
}

void PPS::Eval(PIRKey k, std::set<uint64_t>& set) {
  uint32_t height = Depth(set_size_);
  GGMTree rand(k, height);
  std::vector<uint128_t> leaf_nodes;
  rand.Gen(leaf_nodes);

  if (!set.empty()) {
    set.clear();
  }
  for (uint64_t i = 0; i < set_size_; i++) {
    set.insert(LemireTrick(leaf_nodes[i], universe_size_));
  }
}

void PPS::Eval(PIRKey k, std::unordered_set<uint64_t>& set) {
  uint32_t height = Depth(set_size_);
  GGMTree rand(k, height);
  std::vector<uint128_t> leaf_nodes;
  rand.Gen(leaf_nodes);

  if (!set.empty()) {
    set.clear();
  }
  for (uint64_t i = 0; i < set_size_; i++) {
    set.insert(LemireTrick(leaf_nodes[i], universe_size_));
  }
}

void PPS::Eval(const PIRPuncKey& sk_punc, std::set<uint64_t>& set) {
  uint32_t height = Depth(set_size_);
  std::vector<uint128_t> leaf_nodes(1 << height);
  leaf_nodes[0] = 0;
  std::vector<uint128_t> temp((1 << height));
  for (uint32_t depth = 0; depth < height; ++depth) {
    std::copy(leaf_nodes.begin(), leaf_nodes.begin() + (1 << depth),
              temp.begin());
    for (uint32_t i = 0; i < (1u << depth); ++i) {
      if (temp[i]) {
        GGMTree::AESPRG(temp[i], leaf_nodes[2 * i], leaf_nodes[2 * i + 1]);
      } else {
        leaf_nodes[2 * i + 1] = leaf_nodes[2 * i] = 0;
      }
    }
    leaf_nodes[(sk_punc.pos_ >> (height - 1 - depth)) ^ 1u] = sk_punc.k_[depth];
  }

  if (!set.empty()) {
    set.clear();
  }
  for (uint64_t i = 0; i < set_size_; i++) {
    if (i != sk_punc.pos_) {
      set.insert(LemireTrick(leaf_nodes[i], universe_size_));
    }
  }
}

}  // namespace pir::pps
