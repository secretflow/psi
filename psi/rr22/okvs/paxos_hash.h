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

#include <memory>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "libdivide.h"
#include "yacl/math/gadget.h"
#include "yacl/utils/platform_utils.h"

#include "psi/rr22/okvs/aes_crhash.h"
#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22::okvs {

namespace {

constexpr uint64_t DivCeil(uint64_t val, uint64_t d) {
  return (val + d - 1) / d;
}
constexpr uint64_t RoundUpTo(uint64_t val, uint64_t step) {
  return DivCeil(val, step) * step;
}

using block256 = std::array<uint128_t, 2>;

inline block256 _mm256_loadu_si256(block256* p) {
  block256 b256;
  std::memcpy(&b256, p, sizeof(block256));
  return b256;
}

inline block256 my_libdivide_u64_do_vec256(
    const block256& x, const libdivide::libdivide_u64_t* divider) {
  block256 y;

  auto x64 = (uint64_t*)&x;
  auto y64 = (uint64_t*)&y;

  y64[0] = libdivide::libdivide_u64_do(x64[0], divider);
  y64[1] = libdivide::libdivide_u64_do(x64[1], divider);
  y64[2] = libdivide::libdivide_u64_do(x64[2], divider);
  y64[3] = libdivide::libdivide_u64_do(x64[3], divider);

  return y;
}

inline void DoMod32(uint64_t* vals, const libdivide::libdivide_u64_t* divider,
                    const uint64_t& mod_val) {
  uint64_t i = 0;

#ifdef __x86_64__
  block256 row256a = _mm256_loadu_si256((block256*)&vals[i]);
  block256 row256b = _mm256_loadu_si256((block256*)&vals[i + 4]);
  block256 row256c = _mm256_loadu_si256((block256*)&vals[i + 8]);
  block256 row256d = _mm256_loadu_si256((block256*)&vals[i + 12]);
  block256 row256e = _mm256_loadu_si256((block256*)&vals[i + 16]);
  block256 row256f = _mm256_loadu_si256((block256*)&vals[i + 20]);
  block256 row256g = _mm256_loadu_si256((block256*)&vals[i + 24]);
  block256 row256h = _mm256_loadu_si256((block256*)&vals[i + 28]);
#else
  block256 row256a;
  block256 row256b;
  block256 row256c;
  block256 row256d;
  block256 row256e;
  block256 row256f;
  block256 row256g;
  block256 row256h;
  std::memcpy(&row256a, &vals[i], sizeof(block256));
  std::memcpy(&row256b, &vals[i + 4], sizeof(block256));
  std::memcpy(&row256c, &vals[i + 8], sizeof(block256));
  std::memcpy(&row256d, &vals[i + 12], sizeof(block256));
  std::memcpy(&row256e, &vals[i + 16], sizeof(block256));
  std::memcpy(&row256f, &vals[i + 20], sizeof(block256));
  std::memcpy(&row256g, &vals[i + 24], sizeof(block256));
  std::memcpy(&row256h, &vals[i + 28], sizeof(block256));
#endif

  auto tempa = my_libdivide_u64_do_vec256(row256a, divider);
  auto tempb = my_libdivide_u64_do_vec256(row256b, divider);
  auto tempc = my_libdivide_u64_do_vec256(row256c, divider);
  auto tempd = my_libdivide_u64_do_vec256(row256d, divider);
  auto tempe = my_libdivide_u64_do_vec256(row256e, divider);
  auto tempf = my_libdivide_u64_do_vec256(row256f, divider);
  auto tempg = my_libdivide_u64_do_vec256(row256g, divider);
  auto temph = my_libdivide_u64_do_vec256(row256h, divider);

  auto temp64a = (uint64_t*)&tempa;
  auto temp64b = (uint64_t*)&tempb;
  auto temp64c = (uint64_t*)&tempc;
  auto temp64d = (uint64_t*)&tempd;
  auto temp64e = (uint64_t*)&tempe;
  auto temp64f = (uint64_t*)&tempf;
  auto temp64g = (uint64_t*)&tempg;
  auto temp64h = (uint64_t*)&temph;

  vals[i + 0] -= temp64a[0] * mod_val;
  vals[i + 1] -= temp64a[1] * mod_val;
  vals[i + 2] -= temp64a[2] * mod_val;
  vals[i + 3] -= temp64a[3] * mod_val;
  vals[i + 4] -= temp64b[0] * mod_val;
  vals[i + 5] -= temp64b[1] * mod_val;
  vals[i + 6] -= temp64b[2] * mod_val;
  vals[i + 7] -= temp64b[3] * mod_val;
  vals[i + 8] -= temp64c[0] * mod_val;
  vals[i + 9] -= temp64c[1] * mod_val;
  vals[i + 10] -= temp64c[2] * mod_val;
  vals[i + 11] -= temp64c[3] * mod_val;
  vals[i + 12] -= temp64d[0] * mod_val;
  vals[i + 13] -= temp64d[1] * mod_val;
  vals[i + 14] -= temp64d[2] * mod_val;
  vals[i + 15] -= temp64d[3] * mod_val;
  vals[i + 16] -= temp64e[0] * mod_val;
  vals[i + 17] -= temp64e[1] * mod_val;
  vals[i + 18] -= temp64e[2] * mod_val;
  vals[i + 19] -= temp64e[3] * mod_val;
  vals[i + 20] -= temp64f[0] * mod_val;
  vals[i + 21] -= temp64f[1] * mod_val;
  vals[i + 22] -= temp64f[2] * mod_val;
  vals[i + 23] -= temp64f[3] * mod_val;
  vals[i + 24] -= temp64g[0] * mod_val;
  vals[i + 25] -= temp64g[1] * mod_val;
  vals[i + 26] -= temp64g[2] * mod_val;
  vals[i + 27] -= temp64g[3] * mod_val;
  vals[i + 28] -= temp64h[0] * mod_val;
  vals[i + 29] -= temp64h[1] * mod_val;
  vals[i + 30] -= temp64h[2] * mod_val;
  vals[i + 31] -= temp64h[3] * mod_val;

  return;
}

};  // namespace

template <typename IdxType>
struct PaxosHash {
  uint64_t weight;
  uint64_t sparse_size;
  uint64_t idx_size;

  std::shared_ptr<AesCrHash> aes_crhash;

  std::vector<libdivide::libdivide_u64_t> mods;

  std::vector<uint64_t> mod_vals;

  void init(uint128_t seed, uint64_t weight, uint64_t paxos_size) {
    this->weight = weight;
    this->sparse_size = paxos_size;
    this->idx_size = static_cast<IdxType>(
        RoundUpTo(yacl::math::Log2Ceil(sparse_size), 8) / 8);

    aes_crhash = std::make_shared<AesCrHash>(seed);

    mod_vals.resize(weight);
    mods.resize(weight);

    for (uint64_t i = 0; i < weight; ++i) {
      mod_vals[i] = sparse_size - i;
      mods[i] = libdivide::libdivide_u64_gen(mod_vals[i]);
    }
  }

  void mod32(uint64_t* vals, uint64_t mod_idx) const;

  void HashBuildRow32(const absl::Span<const uint128_t> input,
                      absl::Span<IdxType> rows,
                      absl::Span<uint128_t> hash) const;

  void BuildRow32(const absl::Span<uint128_t> hash,
                  absl::Span<IdxType> rows) const;

  void HashBuildRow1(const uint128_t& input, absl::Span<IdxType> rows,
                     uint128_t* hash) const;

  void BuildRow(const uint128_t& hash, absl::Span<IdxType> rows) const;
};

}  // namespace psi::rr22::okvs
