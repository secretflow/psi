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

#include "psi/rr22/okvs/paxos_hash.h"

#include <algorithm>

#include "yacl/base/int128.h"
#include "yacl/utils/platform_utils.h"

namespace psi::rr22::okvs {

template <typename IdxType>
void PaxosHash<IdxType>::mod32(uint64_t* vals, uint64_t mod_idx) const {
  auto divider = &mods[mod_idx];
  auto mod_val = mod_vals[mod_idx];

  DoMod32(vals, divider, mod_val);
}

template <typename IdxType>
void PaxosHash<IdxType>::BuildRow32(const absl::Span<uint128_t> hash,
                                    absl::Span<IdxType> rows) const {
  if ((weight == 3) && yacl::hasAVX2()) {
#ifdef __x86_64__
    yacl::block row128_[3][16];

    for (uint64_t i = 0; i < weight; ++i) {
      auto ll = (uint64_t*)row128_[i];

      for (uint64_t j = 0; j < 32; ++j) {
        std::memcpy(&ll[j], (uint8_t*)(&hash[j]) + sizeof(uint32_t) * i,
                    sizeof(uint64_t));
      }
      mod32(ll, i);
    }

    for (uint64_t i = 0; i < 2; ++i) {
      std::array<yacl::block, 8> mask, max, min;

      std::array<yacl::block*, 3> row128{row128_[0] + i * 8, row128_[1] + i * 8,
                                         row128_[2] + i * 8};

      // mask = a > b ? -1 : 0;
      mask[0] = _mm_cmpgt_epi64(row128[0][0], row128[1][0]);
      mask[1] = _mm_cmpgt_epi64(row128[0][1], row128[1][1]);
      mask[2] = _mm_cmpgt_epi64(row128[0][2], row128[1][2]);
      mask[3] = _mm_cmpgt_epi64(row128[0][3], row128[1][3]);
      mask[4] = _mm_cmpgt_epi64(row128[0][4], row128[1][4]);
      mask[5] = _mm_cmpgt_epi64(row128[0][5], row128[1][5]);
      mask[6] = _mm_cmpgt_epi64(row128[0][6], row128[1][6]);
      mask[7] = _mm_cmpgt_epi64(row128[0][7], row128[1][7]);

      min[0] = row128[0][0] ^ row128[1][0];
      min[1] = row128[0][1] ^ row128[1][1];
      min[2] = row128[0][2] ^ row128[1][2];
      min[3] = row128[0][3] ^ row128[1][3];
      min[4] = row128[0][4] ^ row128[1][4];
      min[5] = row128[0][5] ^ row128[1][5];
      min[6] = row128[0][6] ^ row128[1][6];
      min[7] = row128[0][7] ^ row128[1][7];

      // max = max(a,b)
      max[0] = (min[0]) & mask[0];
      max[1] = (min[1]) & mask[1];
      max[2] = (min[2]) & mask[2];
      max[3] = (min[3]) & mask[3];
      max[4] = (min[4]) & mask[4];
      max[5] = (min[5]) & mask[5];
      max[6] = (min[6]) & mask[6];
      max[7] = (min[7]) & mask[7];
      max[0] = max[0] ^ row128[1][0];
      max[1] = max[1] ^ row128[1][1];
      max[2] = max[2] ^ row128[1][2];
      max[3] = max[3] ^ row128[1][3];
      max[4] = max[4] ^ row128[1][4];
      max[5] = max[5] ^ row128[1][5];
      max[6] = max[6] ^ row128[1][6];
      max[7] = max[7] ^ row128[1][7];

      // min = min(a,b)
      min[0] = min[0] ^ max[0];
      min[1] = min[1] ^ max[1];
      min[2] = min[2] ^ max[2];
      min[3] = min[3] ^ max[3];
      min[4] = min[4] ^ max[4];
      min[5] = min[5] ^ max[5];
      min[6] = min[6] ^ max[6];
      min[7] = min[7] ^ max[7];

      // if (max == b)
      //   ++b
      //   ++max
      mask[0] = _mm_cmpeq_epi64(max[0], row128[1][0]);
      mask[1] = _mm_cmpeq_epi64(max[1], row128[1][1]);
      mask[2] = _mm_cmpeq_epi64(max[2], row128[1][2]);
      mask[3] = _mm_cmpeq_epi64(max[3], row128[1][3]);
      mask[4] = _mm_cmpeq_epi64(max[4], row128[1][4]);
      mask[5] = _mm_cmpeq_epi64(max[5], row128[1][5]);
      mask[6] = _mm_cmpeq_epi64(max[6], row128[1][6]);
      mask[7] = _mm_cmpeq_epi64(max[7], row128[1][7]);
      row128[1][0] = _mm_sub_epi64(row128[1][0], mask[0]);
      row128[1][1] = _mm_sub_epi64(row128[1][1], mask[1]);
      row128[1][2] = _mm_sub_epi64(row128[1][2], mask[2]);
      row128[1][3] = _mm_sub_epi64(row128[1][3], mask[3]);
      row128[1][4] = _mm_sub_epi64(row128[1][4], mask[4]);
      row128[1][5] = _mm_sub_epi64(row128[1][5], mask[5]);
      row128[1][6] = _mm_sub_epi64(row128[1][6], mask[6]);
      row128[1][7] = _mm_sub_epi64(row128[1][7], mask[7]);
      max[0] = _mm_sub_epi64(max[0], mask[0]);
      max[1] = _mm_sub_epi64(max[1], mask[1]);
      max[2] = _mm_sub_epi64(max[2], mask[2]);
      max[3] = _mm_sub_epi64(max[3], mask[3]);
      max[4] = _mm_sub_epi64(max[4], mask[4]);
      max[5] = _mm_sub_epi64(max[5], mask[5]);
      max[6] = _mm_sub_epi64(max[6], mask[6]);
      max[7] = _mm_sub_epi64(max[7], mask[7]);

      // if (c >= min)
      //   ++c
      mask[0] = _mm_cmpgt_epi64(min[0], row128[2][0]);
      mask[1] = _mm_cmpgt_epi64(min[1], row128[2][1]);
      mask[2] = _mm_cmpgt_epi64(min[2], row128[2][2]);
      mask[3] = _mm_cmpgt_epi64(min[3], row128[2][3]);
      mask[4] = _mm_cmpgt_epi64(min[4], row128[2][4]);
      mask[5] = _mm_cmpgt_epi64(min[5], row128[2][5]);
      mask[6] = _mm_cmpgt_epi64(min[6], row128[2][6]);
      mask[7] = _mm_cmpgt_epi64(min[7], row128[2][7]);
      mask[0] = mask[0] ^ yacl::Uint128Max();
      mask[1] = mask[1] ^ yacl::Uint128Max();
      mask[2] = mask[2] ^ yacl::Uint128Max();
      mask[3] = mask[3] ^ yacl::Uint128Max();
      mask[4] = mask[4] ^ yacl::Uint128Max();
      mask[5] = mask[5] ^ yacl::Uint128Max();
      mask[6] = mask[6] ^ yacl::Uint128Max();
      mask[7] = mask[7] ^ yacl::Uint128Max();
      row128[2][0] = _mm_sub_epi64(row128[2][0], mask[0]);
      row128[2][1] = _mm_sub_epi64(row128[2][1], mask[1]);
      row128[2][2] = _mm_sub_epi64(row128[2][2], mask[2]);
      row128[2][3] = _mm_sub_epi64(row128[2][3], mask[3]);
      row128[2][4] = _mm_sub_epi64(row128[2][4], mask[4]);
      row128[2][5] = _mm_sub_epi64(row128[2][5], mask[5]);
      row128[2][6] = _mm_sub_epi64(row128[2][6], mask[6]);
      row128[2][7] = _mm_sub_epi64(row128[2][7], mask[7]);

      mask[0] = _mm_cmpgt_epi64(max[0], row128[2][0]);
      mask[1] = _mm_cmpgt_epi64(max[1], row128[2][1]);
      mask[2] = _mm_cmpgt_epi64(max[2], row128[2][2]);
      mask[3] = _mm_cmpgt_epi64(max[3], row128[2][3]);
      mask[4] = _mm_cmpgt_epi64(max[4], row128[2][4]);
      mask[5] = _mm_cmpgt_epi64(max[5], row128[2][5]);
      mask[6] = _mm_cmpgt_epi64(max[6], row128[2][6]);
      mask[7] = _mm_cmpgt_epi64(max[7], row128[2][7]);
      mask[0] = mask[0] ^ yacl::Uint128Max();
      mask[1] = mask[1] ^ yacl::Uint128Max();
      mask[2] = mask[2] ^ yacl::Uint128Max();
      mask[3] = mask[3] ^ yacl::Uint128Max();
      mask[4] = mask[4] ^ yacl::Uint128Max();
      mask[5] = mask[5] ^ yacl::Uint128Max();
      mask[6] = mask[6] ^ yacl::Uint128Max();
      mask[7] = mask[7] ^ yacl::Uint128Max();
      row128[2][0] = _mm_sub_epi64(row128[2][0], mask[0]);
      row128[2][1] = _mm_sub_epi64(row128[2][1], mask[1]);
      row128[2][2] = _mm_sub_epi64(row128[2][2], mask[2]);
      row128[2][3] = _mm_sub_epi64(row128[2][3], mask[3]);
      row128[2][4] = _mm_sub_epi64(row128[2][4], mask[4]);
      row128[2][5] = _mm_sub_epi64(row128[2][5], mask[5]);
      row128[2][6] = _mm_sub_epi64(row128[2][6], mask[6]);
      row128[2][7] = _mm_sub_epi64(row128[2][7], mask[7]);

      for (uint64_t j = 0; j < weight; ++j) {
        IdxType* __restrict rowi = rows.data() + weight * 16 * i;
        uint64_t* __restrict row64 = (uint64_t*)(row128[j]);
        rowi[weight * 0 + j] = row64[0];
        rowi[weight * 1 + j] = row64[1];
        rowi[weight * 2 + j] = row64[2];
        rowi[weight * 3 + j] = row64[3];
        rowi[weight * 4 + j] = row64[4];
        rowi[weight * 5 + j] = row64[5];
        rowi[weight * 6 + j] = row64[6];
        rowi[weight * 7 + j] = row64[7];

        rowi += 8 * weight;
        row64 += 8;

        rowi[weight * 0 + j] = row64[0];
        rowi[weight * 1 + j] = row64[1];
        rowi[weight * 2 + j] = row64[2];
        rowi[weight * 3 + j] = row64[3];
        rowi[weight * 4 + j] = row64[4];
        rowi[weight * 5 + j] = row64[5];
        rowi[weight * 6 + j] = row64[6];
        rowi[weight * 7 + j] = row64[7];
      }
    }
#endif
  } else {
    auto rows_ptr = rows.data();

    for (uint64_t k = 0; k < 32; ++k) {
      BuildRow(hash[k], absl::MakeSpan(rows_ptr, weight));
      rows_ptr += weight;
    }
  }
}

template <typename IdxType>
void PaxosHash<IdxType>::BuildRow(const uint128_t& hash,
                                  absl::Span<IdxType> rows) const {
  SPDLOG_DEBUG("weight:{}", weight);

  if (weight == 3) {
    uint32_t* rr = (uint32_t*)&hash;
    auto rr0 = *(uint64_t*)(&rr[0]);
    auto rr1 = *(uint64_t*)(&rr[1]);
    auto rr2 = *(uint64_t*)(&rr[2]);
    rows[0] = (IdxType)(rr0 % sparse_size);
    rows[1] = (IdxType)(rr1 % (sparse_size - 1));
    rows[2] = (IdxType)(rr2 % (sparse_size - 2));

    SPDLOG_DEBUG("rr0:{}, rr1:{}, rr2:{}", rr0, rr1, rr2);
    SPDLOG_DEBUG("rr0:{}, row[1]:{}, row[2]:{}", rows[0], rows[1], rows[2]);

    YACL_ENFORCE(rows[0] < sparse_size);
    YACL_ENFORCE(rows[1] < sparse_size);
    YACL_ENFORCE(rows[2] < sparse_size);

    auto min = std::min<IdxType>(rows[0], rows[1]);
    auto max = rows[0] + rows[1] - min;

    SPDLOG_DEBUG("max:{}, min:{}", max, min);

    if (max == rows[1]) {
      ++rows[1];
      ++max;
    }

    if (rows[2] >= min) {
      ++rows[2];
    }

    if (rows[2] >= max) {
      ++rows[2];
    }

    SPDLOG_DEBUG("max:{}, min:{}", max, min);
    SPDLOG_DEBUG("rr0:{}, row[1]:{}, row[2]:{}", rows[0], rows[1], rows[2]);
  } else {
    Galois128 hh(hash);
    for (uint64_t j = 0; j < weight; ++j) {
      auto modulus = (sparse_size - j);

      hh = hh * hh;

      auto col_idx = hh.get<uint64_t>(0) % modulus;

      auto iter = rows.begin();
      auto end = rows.begin() + j;
      while (iter != end) {
        if (*iter <= col_idx) {
          ++col_idx;
        } else {
          break;
        }
        ++iter;
      }

      while (iter != end) {
        end[0] = end[-1];
        --end;
      }

      *iter = static_cast<IdxType>(col_idx);
    }
  }
}

template <typename IdxType>
void PaxosHash<IdxType>::HashBuildRow32(
    const absl::Span<const uint128_t> in_iter, absl::Span<IdxType> rows,
    absl::Span<uint128_t> hash) const {
  YACL_ENFORCE(in_iter.size() == 32);

  YACL_ENFORCE(rows.size() == 32 * weight);

  aes_crhash->Hash(in_iter, hash);
  BuildRow32(hash, rows);
}

template <typename IdxType>
void PaxosHash<IdxType>::HashBuildRow1(const uint128_t& input,
                                       absl::Span<IdxType> rows,
                                       uint128_t* hash) const {
  YACL_ENFORCE(rows.size() == weight);

  aes_crhash->Hash(absl::MakeSpan(&input, 1), absl::MakeSpan(hash, 1));

  BuildRow(*hash, rows);
}

template struct PaxosHash<uint8_t>;
template struct PaxosHash<uint16_t>;
template struct PaxosHash<uint32_t>;
template struct PaxosHash<uint64_t>;
template struct PaxosHash<uint128_t>;

}  // namespace psi::rr22::okvs
