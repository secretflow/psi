#include <random>
#include <set>

#include "ggm_pset.h"
#include "yacl/crypto/rand/rand.h"

namespace pir::pps {

class PpsPirServer {
 public:
  PpsPirServer() : pps_(), universe_size_(0), set_size_(0) {}

  PpsPirServer(uint64_t universe_size, uint64_t set_size)
      : pps_(universe_size, set_size),
        universe_size_(universe_size),
        set_size_(set_size) {}

  void Hint(PIRKey k, std::set<uint64_t>& deltas, yacl::dynamic_bitset<>& bits,
            yacl::dynamic_bitset<>& h);

  bool Answer(PIRPuncKey& sk_punc, yacl::dynamic_bitset<>& bits);

  void Hint(std::vector<PIRKeyUnion>& ck, yacl::dynamic_bitset<>& bits,
            yacl::dynamic_bitset<>& h);

  void AnswerMulti(PIRPuncKey& punc_l, PIRPuncKey& punc_r, bool& a_left,
                   bool& a_right, yacl::dynamic_bitset<>& bits);

 private:
  PPS pps_;
  uint64_t universe_size_;
  uint64_t set_size_;
};
}  // namespace pir::pps
