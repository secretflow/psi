#include <random>
#include <set>

#include "ggm_pset.h"
#include "yacl/crypto/rand/rand.h"

namespace pir::pps {

struct PIRQueryParam {
  uint64_t j_;
  bool b_;
};

class PpsPirClient {
 public:
  PpsPirClient() : pps_(), lambda_(0), universe_size_(0), set_size_(0) {}

  PpsPirClient(uint32_t lambda, uint64_t universe_size, uint64_t set_size)
      : pps_(universe_size, set_size),
        lambda_(lambda),
        universe_size_(universe_size),
        set_size_(set_size) {}

  // Get m = (n / s(n)) * log(n)
  uint64_t M() {
    uint64_t q = universe_size_ / set_size_;
    uint64_t r = universe_size_ % set_size_;
    if (r) {
      q += 1;
    }
    return q * Depth(universe_size_);
  }

  // sample a bit b from Bernoulli((s - 1) / n)
  bool Bernoulli();

  // sample a random from [0, universe_size_]
  uint64_t UniformUint64();

  // Setup(1^\lambda, universe_size_) -> ck, q_h
  void Setup(PIRKey& sk, std::set<uint64_t>& deltas);

  // Query(ck, i \in [n]) -> q \in K_p
  void Query(uint64_t i, PIRKey& sk, std::set<uint64_t>& deltas,
             PIRQueryParam& param, PIRPuncKey& sk_punc);

  // Reconstruct(h ∈ {0, 1}^m, a ∈ {0, 1}) → x_i
  int Reconstruct(PIRQueryParam& param, yacl::dynamic_bitset<>& h, bool a,
                  bool& r);

  // Get m = (2 * n / s(n)) * log(n)
  uint64_t MM() {
    uint64_t q = 2 * universe_size_ / set_size_;
    uint64_t r = 2 * universe_size_ % set_size_;
    if (r) {
      q += 1;
    }
    return q * Depth(universe_size_);
  }

  // Construction 44 (Multi-query offline/online PIR)
  // Setup(1^λ, n) → (ck, q_h)
  void Setup(std::vector<PIRKeyUnion>& ck,
             std::vector<std::unordered_set<uint64_t>>& v);

  // Query(ck, i) → (ck, q_left, q_right)
  void Query(uint64_t i, std::vector<PIRKeyUnion>& ck,
             std::vector<std::unordered_set<uint64_t>>& v, PIRQueryParam& param,
             PIRPuncKey& punc_l, PIRPuncKey& punc_r);

  // Reconstruct(h, a_left, a_right) → (h′, x_i)
  int Reconstruct(PIRQueryParam& param, yacl::dynamic_bitset<>& h, bool a_left,
                  bool a_right, bool& r);

 private:
  PPS pps_;
  uint32_t lambda_;
  uint64_t universe_size_;
  uint64_t set_size_;
};
}  // namespace pir::pps
