#include "server.h"

namespace pir::pps {

void PpsPirServer::Hint(PIRKey k, std::set<uint64_t>& deltas,
                        yacl::dynamic_bitset<>& bits,
                        yacl::dynamic_bitset<>& h) {
  std::set<uint64_t> set;
  pps_.Eval(k, set);
  h.resize(deltas.size());
  h.reset();
  std::set<uint64_t>::iterator iter = deltas.begin();
  for (uint64_t j = 0; iter != deltas.end(); ++j, ++iter) {
    for (uint64_t value : set) {
      h[j] ^= bits[MODULE_ADD(value, *iter, universe_size_)];
    }
  }
}

bool PpsPirServer::Answer(PIRPuncKey& sk_punc, yacl::dynamic_bitset<>& bits) {
  std::set<uint64_t> set;
  pps_.Eval(sk_punc, set);

  bool r = 0;
  for (uint64_t value : set) {
    r ^= bits[MODULE_ADD(value, sk_punc.delta_, universe_size_)];
  }
  return r;
}

void PpsPirServer::Hint(std::vector<PIRKeyUnion>& ck,
                        yacl::dynamic_bitset<>& bits,
                        yacl::dynamic_bitset<>& h) {
  std::set<uint64_t> set;
  h.resize(ck.size());
  h.reset();
  for (uint64_t j = 0; j < ck.size(); ++j) {
    pps_.Eval(ck[j].k_, set);
    for (uint64_t value : set) {
      h[j] ^= bits[MODULE_ADD(value, ck[j].delta_, universe_size_)];
    }
    set.clear();
  }
}

void PpsPirServer::AnswerMulti(PIRPuncKey& punc_l, PIRPuncKey& punc_r,
                               bool& a_left, bool& a_right,
                               yacl::dynamic_bitset<>& bits) {
  a_left = Answer(punc_l, bits);
  if ((punc_l.pos_ == punc_r.pos_) && (punc_l.k_[0] == punc_r.k_[0])) {
    a_right = a_left;
  } else {
    a_right = Answer(punc_r, bits);
  }
}
}  // namespace pir::pps
