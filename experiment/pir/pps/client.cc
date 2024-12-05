// Copyright 2024 The secretflow authors.
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

#include "client.h"

#include <spdlog/spdlog.h>

namespace pir::pps {

bool PpsPirClient::Bernoulli() {
  std::random_device rand;
  std::mt19937 gen(rand());
  double p =
      static_cast<double>(set_size_ - 1) / static_cast<double>(universe_size_);
  std::bernoulli_distribution distribution(p);
  return distribution(gen);
}

uint64_t PpsPirClient::GetRandomU64Less() {
  return LemireTrick(yacl::crypto::RandU64(), set_size_);
}

// Generate sk and m random numbers \in [n]
void PpsPirClient::Setup(PIRKey& sk, std::set<uint64_t>& deltas) {
  sk = pps_.Gen(lambda_);
  // The map.size() must be equal to SET_SIZE.
  std::vector<uint64_t> rand =
      yacl::crypto::PrgAesCtr<uint64_t>(yacl::crypto::RandU64(), M());
  for (uint64_t i = 0; i < M(); i++) {
    // The most expensive operation.
    uint64_t r = LemireTrick(rand[i], universe_size_);
    if (!deltas.insert(r).second) {
      rand[i] = yacl::crypto::RandU64();
      i--;
    }
  }
}

// Params:
// For input index i_{pir} \in [n], ouput punc key.
void PpsPirClient::Query(uint64_t i, PIRKey& sk, std::set<uint64_t>& deltas,
                         PIRQueryParam& param, PIRPuncKey& sk_punc) {
  std::set<uint64_t>::iterator iter = deltas.begin();
  const PIREvalMap map = pps_.getMap();
  for (param.j_ = 0; iter != deltas.end(); ++param.j_, ++iter) {
    uint64_t r = MODULE_SUB(i, *iter, universe_size_);
    if (map.find(r) != map.end()) {
      sk_punc.delta_ = *iter;
      break;
    }
  }
  if (iter == deltas.end()) {
    SPDLOG_INFO("Can't find a j \\in m such that i - \\delta_j \\in Eval(sk)");
    param.j_ = PIR_ABORT;
    auto map_iter = std::next(map.begin(), GetRandomU64Less());
    sk_punc.delta_ = MODULE_SUB(i, map_iter->first, universe_size_);
  }

  uint64_t i_punc;
  if ((param.b_ = Bernoulli())) {
    auto map_iter = std::next(map.begin(), GetRandomU64Less());
    i_punc = map_iter->first;
  } else {
    i_punc = MODULE_SUB(i, sk_punc.delta_, universe_size_);
  }
  pps_.Punc(i_punc, sk, sk_punc);
}

uint64_t PpsPirClient::Reconstruct(PIRQueryParam& param,
                                   yacl::dynamic_bitset<>& h, bool a, bool& r) {
  if (param.b_ || (param.j_ == PIR_ABORT)) {
    SPDLOG_INFO("Reconstruct: Param b == 1 OR j == \\abort");
    return PIR_ABORT;
  }
  r = a ^ h[param.j_];
  return PIR_OK;
}

void PpsPirClient::Setup(std::vector<PIRKeyUnion>& ck,
                         std::vector<std::unordered_set<uint64_t>>& v) {
  ck.resize(MM());
  v.resize(MM());
  std::vector<uint128_t> rand =
      yacl::crypto::PrgAesCtr<uint128_t>(yacl::crypto::RandU128(), MM());
  for (uint64_t i = 0; i < MM(); ++i) {
    pps_.Eval(rand[i], v[i]);
    if (v[i].size() == set_size_) {
      ck[i] = PIRKeyUnion(rand[i]);
    } else {
      v[i].clear();
      rand[i] = yacl::crypto::RandU128();
      --i;
    }
  }
}

void PpsPirClient::Query(uint64_t i, std::vector<PIRKeyUnion>& ck,
                         std::vector<std::unordered_set<uint64_t>>& v,
                         PIRQueryParam& param, PIRPuncKey& punc_l,
                         PIRPuncKey& punc_r) {
  // GenWith(1^\lambda, n, i)

  PIRKey k_new = pps_.Gen(lambda_);
  PIRKeyUnion k_right;
  uint64_t rand_i = std::next(pps_.getMap().begin(), GetRandomU64Less())->first;
  punc_l.delta_ = MODULE_SUB(i, rand_i, universe_size_);

  uint64_t i_punc;
  param.j_ = PIR_ABORT;
  if ((param.b_ = Bernoulli())) {
    rand_i = std::next(pps_.getMap().begin(), GetRandomU64Less())->first;
    i_punc = MODULE_ADD(rand_i, punc_l.delta_, universe_size_);
  } else {
    for (param.j_ = 0; param.j_ < ck.size(); ++param.j_) {
      rand_i = MODULE_SUB(i, ck[param.j_].delta_, universe_size_);
      if (v[param.j_].find(rand_i) != v[param.j_].end()) {
        k_right = PIRKeyUnion(ck[param.j_].k_, ck[param.j_].delta_);
        ck[param.j_] = PIRKeyUnion(k_new, punc_l.delta_);
        v[param.j_].clear();
        pps_.Eval(k_new, v[param.j_]);
        break;
      }
    }

    if (param.j_ == ck.size()) {
      SPDLOG_INFO("Can't find a j \\in [m] such that i \\in Eval(sk_j)");
      param.j_ = PIR_ABORT;
    }
    i_punc = i;
  }

  uint64_t i_punc_l = MODULE_SUB(i_punc, punc_l.delta_, universe_size_);
  pps_.Punc(i_punc_l, k_new, punc_l);
  if (param.j_ != PIR_ABORT) {
    pps_.EvalMap(k_right.k_);
    uint64_t i_punc_r = MODULE_SUB(i_punc, k_right.delta_, universe_size_);
    pps_.Punc(i_punc_r, k_right.k_, punc_r);
    punc_r.delta_ = k_right.delta_;
  } else {
    punc_r = punc_l;
  }
}

uint64_t PpsPirClient::Reconstruct(PIRQueryParam& param,
                                   yacl::dynamic_bitset<>& h, bool a_left,
                                   bool a_right, bool& r) {
  if (param.j_ != PIR_ABORT) {
    r = a_right ^ h[param.j_];
    h[param.j_] = a_left ^ r;
    return PIR_OK;
  }
  SPDLOG_INFO("Reconstruct: j == \\abort");
  return PIR_ABORT;
}
}  // namespace pir::pps
