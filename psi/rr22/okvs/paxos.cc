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

#include "psi/rr22/okvs/paxos.h"

#include <cmath>
#include <functional>
#include <set>

#include "yacl/base/exception.h"

namespace psi::rr22::okvs {

namespace {

constexpr uint8_t kPaxosBuildRowSize = 32;

inline std::vector<uint128_t> MatrixGf128Inv(std::vector<uint128_t> mtx,
                                             size_t row_size, size_t col_size) {
  YACL_ENFORCE(row_size == col_size);

  auto inv = std::vector<uint128_t>(row_size, col_size);
  for (uint64_t i = 0; i < row_size; ++i) {
    inv[i * row_size + i] = yacl::MakeUint128(0, 1);
  }

  uint128_t zero_block = yacl::MakeUint128(0, 0);
  uint128_t one_block = yacl::MakeUint128(0, 1);

  // for the ith row
  for (uint64_t i = 0; i < row_size; ++i) {
    // if the i,i position is zero,
    // then find a lower row to swap with.

    if (mtx[i * row_size + i] == 0) {
      // for all lower row j, check if (j,i)
      // is non-zero
      for (uint64_t j = i + 1; j < row_size; ++j) {
        if (mtx[j * row_size + i] == one_block) {
          // found one, swap the rows and break
          for (uint64_t k = 0; k < row_size; ++k) {
            std::swap(mtx[i * row_size + k], mtx[j * row_size + k]);
            std::swap(inv[i * row_size + k], inv[j * row_size + k]);
          }
          break;
        }
      }

      // double check that we found a swap. If not,
      // then this matrix is not invertable. Return
      // the empty matrix to denote this.
      if (mtx[i * row_size + i] == zero_block) {
        return {};
      }
    }

    // Make the i'th column the zero column with
    // a one on the diagonal. First we will make row
    // i have a one on the diagonal by computing
    //  row(i) = row(i) / entry(i,i)
    Galois128 mtx_ii_inv = Galois128(mtx[i * row_size + i]).Inv();
    for (uint64_t j = 0; j < row_size; ++j) {
      mtx[i * row_size + j] =
          (mtx_ii_inv * mtx[i * row_size + j]).get<uint128_t>(0);
      inv[i * row_size + j] =
          (mtx_ii_inv * inv[i * row_size + j]).get<uint128_t>(0);
    }

    // next we will make all other rows have a zero
    // in the i'th column by computing
    //  row(j) = row(j) - row(i) * entry(j,i)
    for (uint64_t j = 0; j < row_size; ++j) {
      if (j != i) {
        Galois128 mtx_ji(mtx[j * row_size + i]);
        for (uint64_t k = 0; k < row_size; ++k) {
          mtx[j * row_size + k] =
              mtx[j * row_size + k] ^
              (mtx_ji * mtx[i * row_size + k]).get<uint128_t>(0);
          inv[j * row_size + k] =
              inv[j * row_size + k] ^
              (mtx_ji * inv[i * row_size + k]).get<uint128_t>(0);
        }
      }
    }
  }

  return inv;
}

[[maybe_unused]] inline std::vector<uint128_t> MatrixGf128Mul(
    const std::vector<uint128_t>& m0, const std::vector<uint128_t>& m1,
    size_t row_size, size_t col_size) {
  YACL_ENFORCE(row_size == col_size);

  std::vector<uint128_t> ret(row_size, col_size);

  for (uint64_t i = 0; i < row_size; ++i) {
    for (uint64_t j = 0; j < col_size; ++j) {
      auto& v = ret[i * row_size + j];
      for (uint64_t k = 0; k < col_size; ++k) {
        v = v ^ (Galois128(m0[i * row_size + k]) * m1[k * row_size + j])
                    .get<uint128_t>(0);
      }
    }
  }
  return ret;
}

void ithCombination(uint64_t index, uint64_t n, std::vector<uint64_t>& set) {
  //'''Yields the items of the single combination that would be at the provided
  //(0-based) index in a lexicographically sorted list of combinations of
  // choices of k items from n items [0,n), given the combinations were sorted
  // in
  // descending order. Yields in descending order.
  //'''
  uint64_t nCk = 1;
  uint64_t nMinusI = n;
  uint64_t iPlus1 = 1;

  auto k = set.size();

  // nMinusI, iPlus1 in zip(range(n, n - k, -1), range(1, k + 1)):
  for (; nMinusI != n - k; --nMinusI, ++iPlus1) {
    nCk *= nMinusI;
    nCk /= iPlus1;
  }

  // std::cout << "nCk " << nCk << std::endl;

  auto cur_index = nCk;
  for (auto kk = k; kk != 0ull; --kk)  // in range(k, 0, -1):
  {
    // std::cout << "kk " << kk << " " <<  nCk << std::endl;
    nCk *= kk;
    nCk /= n;
    while (cur_index - nCk > index) {
      cur_index -= nCk;
      nCk *= (n - kk);
      nCk -= nCk % kk;
      n -= 1;
      nCk /= n;
    }
    n -= 1;

    set[kk - 1] = n;
  }
}

std::vector<uint64_t> ithCombination(uint64_t index, uint64_t n, uint64_t k) {
  std::vector<uint64_t> set(k);
  ithCombination(index, n, set);
  return set;
}

uint64_t Choose(uint64_t n, uint64_t k) {
  if (k == 0) return 1;
  return (n * Choose(n - 1, k - 1)) / k;
}

}  // namespace

void PaxosParam::Init(uint64_t num_items, uint64_t weight, uint64_t ssp,
                      PaxosParam::DenseType dt) {
  YACL_ENFORCE(weight >= 2, "weight:{} must be 2 or greater.", weight);

  this->weight = weight;
  this->ssp = ssp;
  this->dt = dt;

  double logn = std::log2(num_items);

  if (weight == 2) {
    double a = 7.529;
    double b = 0.61;
    double c = 2.556;
    double lambda_vs_gap = a / (logn - c) + b;

    // lambda = lambdaVsGap * g - 1.9 * lambdaVsGap
    // g = (lambda + 1.9 * lambdaVsGap) /  lambdaVsGap
    //   = lambda / lambdaVsGap + 1.9

    this->g = static_cast<uint64_t>(std::ceil(ssp / lambda_vs_gap + 1.9));

    dense_size =
        this->g + (this->dt == PaxosParam::DenseType::Binary) * this->ssp;
    sparse_size = 2 * num_items;
  } else {
    double ee = 0;
    if (weight == 3) ee = 1.223;
    if (weight == 4) ee = 1.293;
    if (weight >= 5) ee = 0.1485 * weight + 0.6845;

    double logw = std::log2(weight);
    double log_lambda_vs_e = 0.555 * logn + 0.093 * std::pow(logw, 3) -
                             1.01 * std::pow(logw, 2) + 2.925 * logw - 0.133;
    double lambda_vs_e = std::pow(2, log_lambda_vs_e);

    double b = -9.2 - lambda_vs_e * ee;

    double e = (this->ssp - b) / lambda_vs_e;
    this->g =
        std::floor(this->ssp / ((this->weight - 2) * std::log2(e * num_items)));

    dense_size = this->g + (dt == PaxosParam::DenseType::Binary) * this->ssp;
    sparse_size = num_items * e;
  }
}

template <typename IdxType>
void Paxos<IdxType>::Init(uint64_t num_items, PaxosParam p, uint128_t seed) {
  YACL_ENFORCE(
      p.sparse_size < uint64_t(std::numeric_limits<IdxType>::max()),
      "the size of the paxos is too large for the index type. {} vs {}",
      p.sparse_size, uint64_t(std::numeric_limits<IdxType>::max()));

  YACL_ENFORCE(
      (p.sparse_size + p.dense_size) >= num_items,
      "p.sparse_size:{} + p.dense_size:{} should greater than num_items:{}",
      p.sparse_size, p.dense_size, num_items);

  static_cast<PaxosParam&>(*this) = p;
  num_items_ = static_cast<IdxType>(num_items);
  seed_ = seed;
  hasher_.init(seed, weight, sparse_size);
}

template <typename IdxType>
void Paxos<IdxType>::SetInput(absl::Span<const uint128_t> inputs) {
  SPDLOG_DEBUG(
      "setInput begin, inputs.size():{}, mNumItems:{}, mSparseSize:{} "
      "IdxType:{}",
      inputs.size(), num_items_, sparse_size, sizeof(IdxType));

  YACL_ENFORCE(inputs.size() <= num_items_, "inputs size must equal num_items ",
               inputs.size(), num_items_);

  std::vector<IdxType> col_weights(sparse_size);

  dense_.resize(num_items_);
  rows_.resize(num_items_ * weight);
  cols_.resize(sparse_size);
  col_backing_.resize(num_items_ * weight);

  SPDLOG_DEBUG("setInput alloc");

  {
    auto main = inputs.size() / kPaxosBuildRowSize * kPaxosBuildRowSize;
    auto in_iter = inputs.data();
    SPDLOG_DEBUG("main:{}, kPaxosBuildRowSize:{}", main, kPaxosBuildRowSize);

    for (uint64_t i = 0; i < main;
         i += kPaxosBuildRowSize, in_iter += kPaxosBuildRowSize) {
      SPDLOG_DEBUG("i:{}, main:{}", i, main);
      auto rr = &rows_[i * weight];

      YACL_ENFORCE(kPaxosBuildRowSize == 32);

      hasher_.HashBuildRow32(absl::MakeSpan(in_iter, kPaxosBuildRowSize),
                             absl::MakeSpan(rr, kPaxosBuildRowSize * weight),
                             absl::MakeSpan(&dense_[i], kPaxosBuildRowSize));

      absl::Span<IdxType> cols(rr, kPaxosBuildRowSize * weight);
      SPDLOG_DEBUG("i:{} cols size:{} sparse_size:{}", i, cols.size(),
                   sparse_size);
      for (auto c : cols) {
        SPDLOG_DEBUG("colWeights[{}]: {}", c, col_weights[c]);
        ++col_weights[c];
        SPDLOG_DEBUG("colWeights[{}]: {}", c, col_weights[c]);
      }
    }

    SPDLOG_DEBUG("main:{}, mNumItems:{} mDense size:{}", main, num_items_,
                 dense_.size());

    for (size_t i = main; i < num_items_; ++i, ++in_iter) {
      hasher_.HashBuildRow1(
          *in_iter, absl::MakeSpan(&(rows_[i * weight]), weight), &dense_[i]);
      for (size_t j = 0; j < weight; j++) {
        auto c = rows_[i * weight + j];
        SPDLOG_DEBUG("colWeights[{}]: {}", c, col_weights[c]);
        ++col_weights[c];
        SPDLOG_DEBUG("colWeights[{}]: {}", c, col_weights[c]);
      }
    }

    for (uint64_t i = 0; i < num_items_; i++) {
      SPDLOG_DEBUG("[{}]: {}", i,
                   (std::ostringstream() << Galois128(dense_[i])).str());
    }
  }

  // setTimePoint("setInput buildRow");
  SPDLOG_DEBUG("setInput buildRow");

  RebuildColumns(absl::MakeSpan(col_weights), weight * num_items_);
  // setTimePoint("setInput rebuildColumns");
  SPDLOG_DEBUG("setInput rebuildColumns");

  weight_sets_.init(absl::MakeSpan(col_weights));

  SPDLOG_DEBUG(" mWeightSets.mNodes:{}, mWeightSets:{}",
               weight_sets_.nodes.size(), weight_sets_.weight_sets.size());

  SPDLOG_DEBUG("setInput end");
}

template <typename IdxType>
void Paxos<IdxType>::SetInput(MatrixView<IdxType> rows,
                              absl::Span<uint128_t> dense,
                              absl::Span<absl::Span<IdxType>> cols,
                              absl::Span<IdxType> col_backing,
                              absl::Span<IdxType> col_weights) {
  YACL_ENFORCE((rows.rows() == num_items_) && (dense.size() == num_items_));
  YACL_ENFORCE(rows.cols() == weight);
  YACL_ENFORCE(cols.size() == sparse_size);
  YACL_ENFORCE(col_backing.size() == num_items_ * weight);
  YACL_ENFORCE(col_weights.size() == sparse_size);

  rows_.resize(rows.size());
  std::memcpy(rows_.data(), rows.data(), rows.size() * sizeof(IdxType));

  dense_.resize(dense.size());
  std::memcpy(dense_.data(), dense.data(), dense.size() * sizeof(uint128_t));

  cols_.resize(cols.size());
  for (size_t i = 0; i < cols_.size(); ++i) {
    cols_[i] = cols[i];
  }

  col_backing_.resize(col_backing.size());
  std::memcpy(col_backing_.data(), col_backing.data(),
              col_backing.size() * sizeof(IdxType));

  RebuildColumns(col_weights, weight * num_items_);

  weight_sets_.init(col_weights);
}

template <typename IdxType>
void Paxos<IdxType>::RebuildColumns(absl::Span<IdxType> col_weights,
                                    uint64_t total_weight) {
  YACL_ENFORCE(col_backing_.size() == total_weight);

  SPDLOG_DEBUG("mColBacking.size():{} totalWeight:{}", col_backing_.size(),
               total_weight);

  auto col_iter = col_backing_.data();
  for (uint64_t i = 0; i < sparse_size; ++i) {
    cols_[i] = absl::MakeSpan((IdxType*)(col_iter), (IdxType*)(col_iter));
    col_iter += col_weights[i];
    SPDLOG_DEBUG("colWeights[{}]: {} {}", i, col_weights[i],
                 col_iter - col_backing_.data());
  }

  SPDLOG_DEBUG("weight:{} {} {}", weight, col_iter - col_backing_.data(),
               col_backing_.size());
  YACL_ENFORCE(col_iter == (col_backing_.data() + col_backing_.size()));

  if (weight == 3) {
    for (IdxType i = 0; i < num_items_; ++i) {
      auto& c0 = cols_[rows_[i * weight + 0]];
      auto& c1 = cols_[rows_[i * weight + 1]];
      auto& c2 = cols_[rows_[i * weight + 2]];

      auto s0 = c0.size();
      auto s1 = c1.size();
      auto s2 = c2.size();

      auto d0 = c0.data();
      auto d1 = c1.data();
      auto d2 = c2.data();

      c0 = absl::Span<IdxType>(d0, s0 + 1);
      c1 = absl::Span<IdxType>(d1, s1 + 1);
      c2 = absl::Span<IdxType>(d2, s2 + 1);

      SPDLOG_DEBUG("i:{}, s0:{}, s1:{}, s2:{}", i, s0, s1, s2);

      c0[s0] = i;
      c1[s1] = i;
      c2[s2] = i;
    }
  } else {
    for (IdxType i = 0; i < num_items_; ++i) {
      for (size_t j = 0; j < weight; j++) {
        auto& col = cols_[rows_[i * weight + j]];
        auto s = col.size();
        col = absl::Span<IdxType>(col.data(), s + 1);
        col[s] = i;
        SPDLOG_DEBUG("i:{}, j:{}", i, j);
      }
    }
  }
}

template <typename IdxType>
void Paxos<IdxType>::Encode(
    const PxVector& values, PxVector& output, PxVector::Helper& h,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng) {
  YACL_ENFORCE(static_cast<uint64_t>(output.size()) == size(),
               "output.size():{} size():{}", output.size(), size());

  std::vector<IdxType> main_rows, main_cols;
  main_rows.reserve(num_items_);
  main_cols.reserve(num_items_);
  std::vector<std::array<IdxType, 2>> gap_rows;

  Triangulate(main_rows, main_cols, gap_rows);
  SPDLOG_DEBUG("mainRows.size():{}, mainCols.size():{}, gapRows.size():{}",
               main_rows.size(), main_cols.size(), gap_rows.size());
  for (size_t i = 0; i < main_rows.size(); i++) {
    SPDLOG_DEBUG("mainRows[{}]:{} main_cols[{}]:{}", i, main_rows[i], i,
                 main_cols[i]);
  }

  // output.ZeroFill();

  SPDLOG_DEBUG("prng:{}", prng ? "not NULL" : "NULL");
  if (prng) {
    typename WeightData<IdxType>::WeightNode* node =
        weight_sets_.weight_sets[0];
    while (node != nullptr) {
      auto col_idx = weight_sets_.IdxOf(*node);
      // prng->get(output[colIdx], output.stride());
      h.Randomize(output[col_idx], prng);

      if (node->next_weight_node == weight_sets_.NullNode)
        node = nullptr;
      else
        node = weight_sets_.nodes.data() + node->next_weight_node;
    }
  }

  Backfill(absl::MakeSpan(main_rows), absl::MakeSpan(main_cols),
           absl::MakeSpan(gap_rows), values, output, h, prng);
}

template <typename IdxType>
void Paxos<IdxType>::Triangulate(
    std::vector<IdxType>& main_rows, std::vector<IdxType>& main_cols,
    std::vector<std::array<IdxType, 2>>& gap_rows) {
  SPDLOG_DEBUG("triangulate begin");

  if (weight_sets_.weight_sets.size() <= 1) {
    std::vector<IdxType> col_weights(sparse_size);
    for (uint64_t i = 0; i < cols_.size(); ++i) {
      col_weights[i] = static_cast<IdxType>(cols_[i].size());
    }
    weight_sets_.init(absl::MakeSpan(col_weights));
  }

  std::vector<uint8_t> row_set(num_items_);
  while (weight_sets_.weight_sets.size() > 1) {
    auto& col = weight_sets_.GetMinWeightNode();
    SPDLOG_DEBUG("colIdx:{} col.mWeight:{}", weight_sets_.IdxOf(col),
                 col.weight);

    weight_sets_.PopNode(col);
    col.weight = 0;
    auto col_idx = weight_sets_.IdxOf(col);

    SPDLOG_DEBUG("colIdx:{} col.mWeight:{}", col_idx, col.weight);

    bool first = true;

    // iterate over all the rows in the next column
    for (auto row_idx : cols_[col_idx]) {
      SPDLOG_DEBUG("rowIdx:{} size:{} rowSet[rowIdx]:{}", row_idx,
                   cols_[col_idx].size(), row_set[row_idx]);
      // check if this row has already been set.
      if (row_set[row_idx] == 0) {
        row_set[row_idx] = 1;

        // iterate over the other columns in this row.
        // for (auto col_idx2 : rows_[row_idx]) {
        for (size_t j = 0; j < weight; ++j) {
          auto col_idx2 = rows_[row_idx * weight + j];
          auto& node = weight_sets_.nodes[col_idx2];
          SPDLOG_DEBUG("colIdx2:{} node.mWeight:{}", col_idx2, node.weight);

          // if this column still hasn't been fixed,
          // then decrement it's weight.
          if (node.weight) {
            YACL_ENFORCE(node.weight);

            // decrement the weight.
            weight_sets_.PopNode(node);
            --node.weight;
            SPDLOG_DEBUG("node.mWeight:{}", node.weight);
            weight_sets_.PushNode(node);

            // as an optimization, prefetch this next
            // column if its ready to be used..
            if (node.weight == 1) {
              _mm_prefetch((const char*)&cols_[col_idx2], _MM_HINT_T0);
            }
          }
        }

        // if this is the first row, then we will use
        // it as the row on the diagonal. Otherwise
        // we will place the extra rows in the "gap"
        SPDLOG_DEBUG("first: {}-{}", first, !!(first));
        if (!!(first)) {
          main_cols.push_back(col_idx);
          main_rows.push_back(row_idx);
          first = false;
          SPDLOG_DEBUG("colIdx:{}, rowIdx:{}", col_idx, row_idx);
        } else {
          // check that we dont have duplicates
          YACL_ENFORCE(
              std::memcmp(&(dense_[main_rows.back()]), &(dense_[row_idx]),
                          sizeof(uint128_t)) != 0,
              "Paxos error, Duplicate keys were detected at idx {} {}, key={}",
              main_rows.back(), row_idx, dense_[main_rows.back()]);

          SPDLOG_DEBUG("rowIdx:{}, mainRows.back():{}", row_idx,
                       main_rows.back());
          gap_rows.emplace_back(
              std::array<IdxType, 2>{row_idx, main_rows.back()});
        }
      }
    }

    YACL_ENFORCE(first == false, "first:{}", first);
  }

  SPDLOG_DEBUG("triangulate end");
}

template <typename IdxType>
void Paxos<IdxType>::Backfill(absl::Span<IdxType> main_rows,
                              absl::Span<IdxType> main_cols,
                              absl::Span<std::array<IdxType, 2>> gap_rows,
                              const PxVector& values, PxVector& output,
                              PxVector::Helper& h,
                              const std::shared_ptr<yacl::crypto::Prg<uint8_t>>&
                                  prng) {  // We are solving the system
  //
  // H * P = X
  //
  // for the unknown "P". Divide the
  // matrix into
  //
  //       | A B C |
  //   H = | D E F |
  //
  // where C is a square lower-triangular matrix,
  // E is a dense g*g matrix with the gap rows.
  //
  // In particular, C have columns mainCols and
  // rows mainCols. Rows of E are indexed by
  // { gapRows[i][0] | i in [g] }. The columns of E will
  // consists of a subset of the dense columns.
  //
  // Then compute
  //
  //      |I        0 |					 |I        0 |
  // H' = |-FC^-1   I | * H		 , X' =  |-FC^-1   I | * X
  //
  //    = | A  B  C |					  = | x1  |
  //      | D' E' 0 |						| x2' |
  //
  // where
  //
  //  D' = -FC^-1A + D
  //  E' = -FC^-1B + E
  //  x2' = -FC^-1 x1 + x2
  //
  // We require E' be invertible. There are a
  // few ways of ensuring this.
  //
  // One is to let E' be binary and then have 40
  // extra dense columns and find an invertible E'
  // out of the 2^40 options. This succeeds with
  // Pr 1-2^-40.
  //
  // Another option is to make E,B consist of
  // random elements in a large field. Then E'
  // is invertible  with overwhelming Pr. (1- 1/fieldSize).
  //
  // Observe that there are many solutions. In
  // particular, the first columns corresponding
  // to A,D can be set arbitrarially. Let p = | r p1 p2 |
  // and rewrite the above as
  //
  //   | B  C | * | p1 | = | x1  | - | A  | * | r |
  //   | E' 0 |   | p2 | = | x2' |   | D' |
  //
  // Therefore we have
  //
  //  r <- $ or r = 0
  //  x2' = x2 - D' r - FC^-1 x1
  //  p1  = -E'^-1 x2'
  //  x1' = x1 - A r - B p1
  //  p2  = -C^-1 x1'
  //
  //  P = | r p2 p1 |
  //
  // Here r is either set to zero or random.
  //
  // In the common case g will be zero and then we
  // have H = | A C | and we solve just using back
  // propegation (C is trivially invertible). Ie
  //
  // r <- $ or r = 0
  // x' = x1 - A r
  // p1 = -C^-1 x'
  //
  // P = | r p1 |

  // setTimePoint("backFill begin");
  SPDLOG_DEBUG("backFill begin {}",
               dt == DenseType::GF128 ? "GF128" : "Binary");

  // select the method based on the dense type.
  // Both perform the same basic algorithm,
  if (dt == DenseType::GF128) {
    BackfillGf128(main_rows, main_cols, gap_rows, values, output, h, prng);
  } else {
    BackfillBinary(main_rows, main_cols, gap_rows, values, output, h, prng);
  }

  SPDLOG_DEBUG("backFill end");
}

template <typename IdxType>
void Paxos<IdxType>::BackfillGf128(
    absl::Span<IdxType> main_rows, absl::Span<IdxType> main_cols,
    absl::Span<std::array<IdxType, 2>> gap_rows, const PxVector& values,
    PxVector& output, PxVector::Helper& helper,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng) {
  YACL_ENFORCE(dt == DenseType::GF128);
  auto g = gap_rows.size();
  auto p2 = output.subspan(sparse_size);

  SPDLOG_DEBUG("gapRows size:{}", g);

  YACL_ENFORCE(g <= dense_size, "g:{}, dense_size", g, dense_size);

  if (g) {
    auto fcinv = GetFCInv(main_rows, main_cols, gap_rows);
    auto size = prng ? dense_size : g;

    //      |dense[r0]^1, dense[r0]^2, ... |
    // E =  |dense[r1]^1, dense[r1]^2, ... |
    //      |dense[r2]^1, dense[r2]^2, ... |
    //      ...
    // EE = E - FC^-1 B
    std::vector<uint128_t> EE(size * size);
    // Matrix<int128_t> EE(size, size);

    // xx = x' - FC^-1 x
    PxVector xx = helper.NewVec(size);
    // std::vector<block> xx(size);
    std::vector<uint128_t> fcb(size);

    for (uint64_t i = 0; i < g; ++i) {
      uint128_t e = dense_[gap_rows[i][0]];
      Galois128 ej(e);
      EE[i * size] = e;  //^ fcb;
      for (uint64_t j = 1; j < size; ++j) {
        ej = ej * e;
        EE[i * size + j] = ej.get<uint128_t>(0);
      }

      helper.Assign(xx[i], values[gap_rows[i][0]]);
      for (auto j : fcinv.mtx[i]) {
        helper.Add(xx[i], values[j]);
        auto fcb = dense_[j];
        Galois128 fcbk(fcb);
        EE[i * size] = EE[i * size] ^ fcb;
        for (uint64_t k = 1; k < size; ++k) {
          fcbk = fcbk * fcb;
          EE[i * size + k] = EE[i * size + k] ^ fcbk.get<uint128_t>(0);
        }
      }
    }

    if (prng) {
      for (uint64_t i = g; i < dense_size; ++i) {
        // prng->get<yacl::block>(EE[i]);
        prng->Fill(absl::MakeSpan(&EE[i * size], size * sizeof(uint128_t)));
        helper.Randomize(xx[i], prng);
      }
    }

    EE = MatrixGf128Inv(EE, size, size);

    YACL_ENFORCE(EE.size() > 0);

    // now we compute
    // p' = (E - FC^-1 B)^-1 * (x'-FC^-1 x)
    //    = EE * xx
    for (uint64_t i = 0; i < size; ++i) {
      auto pp = p2[i];
      for (uint64_t j = 0; j < size; ++j) {
        // pp = pp ^ xx[j] * EE(i, j);
        helper.MultAdd(pp, xx[j], EE[i * size + j]);
      }
    }
  } else if (prng) {
    for (uint64_t i = 0; i < static_cast<uint64_t>(p2.size()); ++i)
      helper.Randomize(p2[i], prng);
  }

  auto out_col_iter = main_cols.rbegin();
  auto row_iter = main_rows.rbegin();
  bool do_dense = g || prng;

#define GF128_DENSE_BACKFILL                    \
  if (do_dense) {                               \
    uint128_t d = dense_[i];                    \
    uint128_t x = d;                            \
    helper.MultAdd(y, p2[0], x);                \
                                                \
    for (uint64_t i = 1; i < dense_size; ++i) { \
      x = (Galois128(x) * d).get<uint128_t>(0); \
      helper.MultAdd(y, p2[i], x);              \
    }                                           \
  }

  auto yy = helper.NewElement();
  auto y = helper.AsPtr(yy);

  if (weight == 3) {
    for (uint64_t k = 0; k < main_rows.size(); ++k) {
      auto i = *row_iter;
      auto c = *out_col_iter;
      ++out_col_iter;
      ++row_iter;
      SPDLOG_DEBUG("k:{}, i:{} c:{}", k, i, c);

      // auto y = X[i];
      helper.Assign(y, values[i]);
      SPDLOG_DEBUG("y:{}", absl::BytesToHexString(
                               absl::string_view((char*)y, sizeof(uint128_t))));

      auto row = absl::MakeSpan(&rows_[i * weight], weight);
      auto cc0 = row[0];
      auto cc1 = row[1];
      auto cc2 = row[2];

      //  y = y ^ P[cc0] ^ P[cc1] ^ P[cc2];
      helper.Add(y, output[cc0]);

      SPDLOG_DEBUG("y:{}, P[{}]:{}",
                   absl::BytesToHexString(
                       absl::string_view((char*)y, sizeof(uint128_t))),
                   cc0,
                   absl::BytesToHexString(absl::string_view(
                       (char*)&(*output[cc0]), sizeof(*output[cc0]))));

      helper.Add(y, output[cc1]);

      SPDLOG_DEBUG("y:{}, P[{}]:{}",
                   absl::BytesToHexString(
                       absl::string_view((char*)y, sizeof(uint128_t))),
                   cc1,
                   absl::BytesToHexString(absl::string_view(
                       (char*)&(*output[cc1]), sizeof(*output[cc1]))));
      helper.Add(y, output[cc2]);
      SPDLOG_DEBUG("y:{}, P[{}]:{}",
                   absl::BytesToHexString(
                       absl::string_view((char*)y, sizeof(uint128_t))),
                   cc2,
                   absl::BytesToHexString(absl::string_view(
                       (char*)&(*output[cc2]), sizeof(*output[cc2]))));

      SPDLOG_DEBUG("doDense:{}", do_dense);

      // GF128_DENSE_BACKFILL;
      if (do_dense) {
        uint128_t d = dense_[i];
        uint128_t x = d;
        helper.MultAdd(y, p2[0], x);
        SPDLOG_DEBUG(
            "d:{}, y:{} mDenseSize:{}",
            absl::BytesToHexString(absl::string_view((char*)&d, sizeof(d))),
            absl::BytesToHexString(
                absl::string_view((char*)y, sizeof(uint128_t))),
            dense_size);

        for (uint64_t i = 1; i < dense_size; ++i) {
          x = (Galois128(x) * d).get<uint128_t>(0);
          helper.MultAdd(y, p2[i], x);
          SPDLOG_DEBUG(
              "c:{}, y:{}",
              absl::BytesToHexString(absl::string_view((char*)&c, sizeof(c))),
              absl::BytesToHexString(
                  absl::string_view((char*)y, sizeof(uint128_t))));
        }
      }

      // P[c] = y;
      helper.Assign(output[c], y);
      SPDLOG_DEBUG("P[{}]:{} y:{}", c,
                   absl::BytesToHexString(absl::string_view(
                       (char*)&(*output[c]), sizeof(*output[c]))),
                   absl::BytesToHexString(
                       absl::string_view((char*)y, sizeof(uint128_t))));
    }
  } else {
    for (uint64_t k = 0; k < main_rows.size(); ++k) {
      auto i = *row_iter;
      auto c = *out_col_iter;
      ++out_col_iter;
      ++row_iter;

      // auto y = X[i];
      helper.Assign(y, values[i]);

      auto row = &rows_[i * weight];
      for (uint64_t j = 0; j < weight; ++j) {
        auto cc = row[j];
        helper.Add(y, output[cc]);
        // y = y ^ P[cc];
      }

      GF128_DENSE_BACKFILL;

      // P[c] = y;
      helper.Assign(output[c], y);
    }
  }
}

template <typename IdxType>
void Paxos<IdxType>::BackfillBinary(
    absl::Span<IdxType> main_rows, absl::Span<IdxType> main_cols,
    absl::Span<std::array<IdxType, 2>> gap_rows, const PxVector& X, PxVector& P,
    PxVector::Helper& h,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng) {
  auto gg = gap_rows.size();

  // the dense columns which index the gap.
  std::vector<uint64_t> gap_cols;

  // masks that will be used to select the
  // bits of the dense columns.
  std::vector<uint64_t> dense_masks(g);

  // the dense part of the paxos.
  auto p2 = P.subspan(sparse_size);

  YACL_ENFORCE(gg <= g, "gg:{}<=g:{}", gg, g);

  if (gg) {
    auto fcinv = GetFCInv(main_rows, main_cols, gap_rows);

    // get the columns for the gap which define
    // B, E and therefore EE.
    gap_cols = GetGapCols(fcinv, gap_rows);

    if (prng) {
      RandomizeDenseCols(p2, h, absl::MakeSpan(gap_cols), prng);
    }

    // auto PP =
    //  x2' = x2 - D r - FC^-1 x1
    auto xx2 =
        GetX2Prime(fcinv, absl::MakeSpan(gap_rows), absl::MakeSpan(gap_cols), X,
                   prng ? P : PxVector{}, h);

    // E' = E - FC^-1 B
    DenseMtx EE =
        GetEPrime(fcinv, absl::MakeSpan(gap_rows), absl::MakeSpan(gap_cols));
    auto EEInv = EE.Invert();

    // now we compute
    // p2 = E'^-1            * x2'
    //    = (-FC^-1B + E)^-1 * (x2 - D r - FC^-1 x1)
    for (uint64_t i = 0; i < g; ++i) {
      auto pp = p2[gap_cols[i]];
      // if (pp != oc::ZeroBlock)
      //	throw RTE_LOC;

      for (uint64_t j = 0; j < g; ++j) {
        if (EEInv(i, j)) {
          // pp = pp ^ xx2[j]
          h.Add(pp, xx2[j]);
        }
      }
    }

    for (uint64_t i = 0; i < g; ++i) {
      dense_masks[i] = 1ull << gap_cols[i];
    }

  } else if (prng) {
    for (uint64_t i = 0; i < static_cast<uint64_t>(p2.size()); ++i)
      h.Randomize(p2[i], prng);
  }

  auto out_col_iter = main_cols.rbegin();
  auto row_iter = main_rows.rbegin();

  // get a temporary element.
  auto yy = h.NewElement();

  // get its pointer
  auto y = h.AsPtr(yy);

  for (uint64_t k = 0; k < main_rows.size(); ++k) {
    auto i = *row_iter;
    auto c = *out_col_iter;
    ++out_col_iter;
    ++row_iter;

    // y = X[i]
    h.Assign(y, X[i]);

    auto row = &rows_[i * weight];
    for (uint64_t j = 0; j < weight; ++j) {
      auto cc = row[j];

      // y += X[i]
      h.Add(y, P[cc]);
    }

    YACL_ENFORCE(dense_size <= 64);

    if (prng) {
      auto d = yacl::DecomposeUInt128(dense_[i]).first;
      for (uint64_t j = 0; j < dense_size; ++j) {
        if (d & 1) {
          // y += p2[j]
          h.Add(y, p2[j]);
        }
        d >>= 1;
      }
    } else {
      for (uint64_t j = 0; j < g; ++j) {
        YACL_ENFORCE(dense_size <= 64);

        if (yacl::DecomposeUInt128(dense_[i]).first & dense_masks[j]) {
          h.Add(y, p2[gap_cols[j]]);
        }
      }
    }

    h.Assign(P[c], y);
  }
}

template <typename IdxType>
typename Paxos<IdxType>::FCInv Paxos<IdxType>::GetFCInv(
    absl::Span<IdxType> main_rows, absl::Span<IdxType> main_cols,
    absl::Span<std::array<IdxType, 2>> gap_rows) const {
  // maps the H column indexes to F,C column Indexes
  std::vector<IdxType> col_mapping;
  Paxos<IdxType>::FCInv ret(gap_rows.size());
  auto m = main_rows.size();

  // the input rows are in reverse order compared to the
  // logical algorithm. This inverts the row index.
  auto invert_row_idx = [m](auto i) { return m - i - 1; };

  for (uint64_t i = 0; i < gap_rows.size(); ++i) {
    if (std::memcmp(&(rows_[gap_rows[i][0] * weight]),
                    &(rows_[gap_rows[i][1] * weight]),
                    weight * sizeof(IdxType)) == 0) {
      // special/common case where FC^-1 [i] = 0000100000
      // where the 1 is at position gapRows[i][1]. This code is
      // used to speed up this common case.
      ret.mtx[i].push_back(gap_rows[i][1]);
    } else {
      // for the general case we need to implicitly create the C
      // matrix. The issue is that currently C is defined by mainRows
      // and mainCols and this form isn't ideal for the computation
      // of computing F C^-1. In particular, we will need to know which
      // columns of the overall matrix H live in C. To do this we will construct
      // colMapping. For columns of H that are in C, colMapping will give us
      // the column in C. We only construct this mapping when its needed.
      if (col_mapping.size() == 0) {
        col_mapping.resize(size(), -1);
        for (uint64_t i = 0; i < m; ++i) {
          col_mapping[main_cols[invert_row_idx(i)]] = i;
        }
      }

      // the current row of F. We initialize this as just F_i
      // and then Xor in rows of C until its the zero row.
      std::set<IdxType, std::greater<IdxType>> row;
      for (uint64_t j = 0; j < weight; ++j) {
        auto c1 = rows_[gap_rows[i][0] * weight + j];
        if (col_mapping[c1] != IdxType(-1)) {
          row.insert(col_mapping[c1]);
        }
      }

      while (row.size()) {
        // the column of C, F that we will cancel (by adding
        // the corresponding row of C to F_i. We will pick the
        // row of C as the row with index CCol.
        auto CCol = *row.begin();

        // the row of C we will add to F_i
        auto CRow = CCol;

        // the row of H that we will add to F_i
        auto HRow = main_rows[invert_row_idx(CRow)];
        ret.mtx[i].push_back(HRow);

        for (size_t j = 0; j < weight; ++j) {
          auto HCol = rows_[HRow * weight + j];
          auto CCol2 = col_mapping[HCol];
          SPDLOG_DEBUG("CCol:{}, CCol2:{}", CCol, CCol2);

          if (CCol2 != IdxType(-1)) {
            YACL_ENFORCE(CCol2 <= CCol, "CCol:{}, CCol2:{}", CCol, CCol2);

            // Xor in the row CRow from C into the current
            // row of F
            auto iter = row.find(CCol2);
            if (iter == row.end()) {
              row.insert(CCol2);
            } else {
              row.erase(iter);
            }
          }
        }

        YACL_ENFORCE((row.size() == 0) || (*row.begin() != CCol));
      }
    }
  }

  return ret;
}

template <typename IdxType>
std::vector<uint64_t> Paxos<IdxType>::GetGapCols(
    const FCInv& fcinv, absl::Span<std::array<IdxType, 2>> gap_rows) const {
  if (gap_rows.size() == 0) return {};

  auto g = gap_rows.size();
  uint64_t ci = 0;
  uint64_t e = Choose(dense_size, g);

  // E' = -FC^-1B + E
  DenseMtx EE;

  while (true) {
    // TDOD, make the linear time.
    auto gap_cols = ithCombination(ci, dense_size, g);
    ++ci;
    YACL_ENFORCE(ci <= e, "failed to find invertible matrix. {}");

    EE.resize(g, g);
    for (uint64_t i = 0; i < g; ++i) {
      uint128_t FCB = yacl::MakeUint128(0, 0);
      for (auto c : fcinv.mtx[i]) {
        FCB = FCB ^ dense_[c];
      }

      // EE = E + FC^-1 B
      uint128_t EERow = dense_[gap_rows[i][0]] ^ FCB;
      for (uint64_t j = 0; j < g; ++j) {
        EE(i, j) = *BitIterator((uint8_t*)&EERow, gap_cols[j]);
      }
    }

    auto EEInv = EE.Invert();
    if (EEInv.rows()) {
      return gap_cols;
    }
  }
}

template <typename IdxType>
PxVector Paxos<IdxType>::GetX2Prime(const FCInv& fcinv,
                                    absl::Span<std::array<IdxType, 2>> gap_rows,
                                    absl::Span<uint64_t> gap_cols,
                                    const PxVector& X, const PxVector& P,
                                    PxVector::Helper& helper) {
  YACL_ENFORCE(X.size() == num_items_);
  bool randomized = P.size() != 0;

  auto g = gap_rows.size();
  PxVector xx2 = helper.NewVec(g);

  for (uint64_t i = 0; i < g; ++i) {
    // x2' = x2 - FC^-1 x1
    helper.Assign(xx2[i], X[gap_rows[i][0]]);
    for (auto j : fcinv.mtx[i]) {
      helper.Add(xx2[i], X[j]);
    }
  }

  // x2' = x2 - D' r - FC^-1 x1
  if (randomized) {
    YACL_ENFORCE(P.size() == dense_size + sparse_size);
    auto p2 = P.subspan(sparse_size);

    // note that D' only has a dense part because we
    // assume only duplcate rows in the gap, can
    // therefore the sparse part of D' cancels.
    for (uint64_t i = 0; i < dense_size; ++i) {
      if (std::find(gap_cols.begin(), gap_cols.end(), i) == gap_cols.end()) {
        for (uint64_t j = 0; j < g; ++j) {
          auto dense = dense_[gap_rows[j][0]];  //^ mDense[gapRows[j][1]];
          for (auto k : fcinv.mtx[j]) {
            dense = dense ^ dense_[k];
          }

          if (*BitIterator((uint8_t*)&dense, i)) {
            helper.Add(xx2[j], p2[i]);
          }
        }
      }
    }
  }

  return xx2;
}

template <typename IdxType>
DenseMtx Paxos<IdxType>::GetEPrime(const FCInv& fcinv,
                                   absl::Span<std::array<IdxType, 2>> gap_rows,
                                   absl::Span<uint64_t> gap_cols) {
  auto g = gap_rows.size();

  // E' = E - FC^-1 B
  DenseMtx EE(g, g);

  for (uint64_t i = 0; i < g; ++i) {
    // EERow    = E - FC^-1 B
    uint128_t EERow = dense_[gap_rows[i][0]];
    for (auto j : fcinv.mtx[i]) {
      EERow = EERow ^ dense_[j];
    }

    // select the gap columns bits.
    for (uint64_t j = 0; j < g; ++j) {
      EE(i, j) = *BitIterator((uint8_t*)&EERow, gap_cols[j]);
    }
  }

  return EE;
}

template <typename IdxType>
void Paxos<IdxType>::RandomizeDenseCols(
    PxVector& p2, PxVector::Helper& h, absl::Span<uint64_t> gap_cols,
    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng) {
  YACL_ENFORCE(prng);

  // handle the dense part of D.
  for (uint64_t i = 0; i < dense_size; ++i) {
    if (std::find(gap_cols.begin(), gap_cols.end(), i) == gap_cols.end()) {
      h.Randomize(p2[i], prng);
    }
  }
}

template <typename IdxType>
void Paxos<IdxType>::Decode(absl::Span<const uint128_t> inputs,
                            absl::Span<uint128_t> values,
                            absl::Span<uint128_t> p) {
  PxVector VV(values);
  PxVector PP(p);
  auto h = PP.DefaultHelper();
  Decode(inputs, VV, PP, h);
}

template <typename IdxType>
void Paxos<IdxType>::Decode(absl::Span<const uint128_t> inputs,
                            PxVector& values, const PxVector& PP,
                            PxVector::Helper& h) {
  SPDLOG_DEBUG("decode begin,inputs.size():{}, values.size():{}, PP.size():{}",
               inputs.size(), values.size(), PP.size());

  YACL_ENFORCE(PP.size() == size(), "{} ?= {}", PP.size(), size());

  auto batch_count = inputs.size() / kPaxosBuildRowSize * kPaxosBuildRowSize;

  auto in_iter = inputs.data();

  SPDLOG_DEBUG("add_to_decode_:{}, gPaxosBuildRowSize:{}, mWeight:{}",
               add_to_decode_, kPaxosBuildRowSize, weight);

  std::vector<IdxType> rows(kPaxosBuildRowSize * weight);
  std::vector<uint128_t> dense(kPaxosBuildRowSize);

  if (add_to_decode_) {
    PxVector v = h.NewVec(kPaxosBuildRowSize);

    for (uint64_t i = 0; i < batch_count;
         i += kPaxosBuildRowSize, in_iter += kPaxosBuildRowSize) {
      hasher_.HashBuildRow32(absl::MakeSpan(in_iter, kPaxosBuildRowSize),
                             absl::MakeSpan(rows), absl::MakeSpan(dense));

      Decode32(absl::MakeSpan(rows), absl::MakeSpan(dense),
               absl::MakeSpan(v[0], kPaxosBuildRowSize), PP, h);

      for (uint64_t j = 0; j < kPaxosBuildRowSize; j += 8) {
        h.Add(values[i + j + 0], v[j + 0]);
        h.Add(values[i + j + 1], v[j + 1]);
        h.Add(values[i + j + 2], v[j + 2]);
        h.Add(values[i + j + 3], v[j + 3]);
        h.Add(values[i + j + 4], v[j + 4]);
        h.Add(values[i + j + 5], v[j + 5]);
        h.Add(values[i + j + 6], v[j + 6]);
        h.Add(values[i + j + 7], v[j + 7]);
      }
    }

    for (uint64_t i = batch_count; i < inputs.size(); ++i, ++in_iter) {
      hasher_.HashBuildRow1(*in_iter, absl::MakeSpan(rows.data(), weight),
                            &dense[0]);
      Decode1(absl::MakeSpan(rows.data(), weight), dense[0], v[0], PP, h);
      h.Add(values[i], v[0]);
    }
  } else {
    for (uint64_t i = 0; i < batch_count;
         i += kPaxosBuildRowSize, in_iter += kPaxosBuildRowSize) {
      hasher_.HashBuildRow32(absl::MakeSpan(in_iter, kPaxosBuildRowSize),
                             absl::MakeSpan(rows), absl::MakeSpan(dense));
      Decode32(absl::MakeSpan(rows), absl::MakeSpan(dense),
               absl::MakeSpan(values[i], kPaxosBuildRowSize), PP, h);
    }

    for (uint64_t i = batch_count; i < inputs.size(); ++i, ++in_iter) {
      hasher_.HashBuildRow1(*in_iter, absl::MakeSpan(rows.data(), weight),
                            &dense[0]);
      Decode1(absl::MakeSpan(rows.data(), weight), dense[0], values[i], PP, h);
    }
  }

  SPDLOG_DEBUG("decode done");
}

template <typename IdxType>
void Paxos<IdxType>::Decode32(absl::Span<IdxType> rows_span,
                              absl::Span<uint128_t> dense_span,
                              absl::Span<uint128_t> values_span,
                              const PxVector& p_, const PxVector::Helper& h) {
  const uint128_t* __restrict p = p_[0];

  for (uint64_t j = 0; j < 4; ++j) {
    const IdxType* __restrict rows = rows_span.data() + j * 8 * weight;
    uint128_t* __restrict values = h.IterPlus(values_span.data(), j * 8);

    auto c00 = rows[weight * 0 + 0];
    auto c01 = rows[weight * 1 + 0];
    auto c02 = rows[weight * 2 + 0];
    auto c03 = rows[weight * 3 + 0];
    auto c04 = rows[weight * 4 + 0];
    auto c05 = rows[weight * 5 + 0];
    auto c06 = rows[weight * 6 + 0];
    auto c07 = rows[weight * 7 + 0];

    auto v00 = h.IterPlus(values, 0);
    auto v01 = h.IterPlus(values, 1);
    auto v02 = h.IterPlus(values, 2);
    auto v03 = h.IterPlus(values, 3);
    auto v04 = h.IterPlus(values, 4);
    auto v05 = h.IterPlus(values, 5);
    auto v06 = h.IterPlus(values, 6);
    auto v07 = h.IterPlus(values, 7);

    auto p00 = h.IterPlus(p, c00);
    auto p01 = h.IterPlus(p, c01);
    auto p02 = h.IterPlus(p, c02);
    auto p03 = h.IterPlus(p, c03);
    auto p04 = h.IterPlus(p, c04);
    auto p05 = h.IterPlus(p, c05);
    auto p06 = h.IterPlus(p, c06);
    auto p07 = h.IterPlus(p, c07);

    h.Assign(v00, p00);
    h.Assign(v01, p01);
    h.Assign(v02, p02);
    h.Assign(v03, p03);
    h.Assign(v04, p04);
    h.Assign(v05, p05);
    h.Assign(v06, p06);
    h.Assign(v07, p07);
  }

  for (uint64_t j = 1; j < weight; ++j) {
    for (uint64_t k = 0; k < 4; ++k) {
      const IdxType* __restrict rows = rows_span.data() + k * 8 * weight;
      uint128_t* __restrict values = h.IterPlus(values_span.data(), k * 8);

      auto c0 = rows[weight * 0 + j];
      auto c1 = rows[weight * 1 + j];
      auto c2 = rows[weight * 2 + j];
      auto c3 = rows[weight * 3 + j];
      auto c4 = rows[weight * 4 + j];
      auto c5 = rows[weight * 5 + j];
      auto c6 = rows[weight * 6 + j];
      auto c7 = rows[weight * 7 + j];

      auto v0 = h.IterPlus(values, 0);
      auto v1 = h.IterPlus(values, 1);
      auto v2 = h.IterPlus(values, 2);
      auto v3 = h.IterPlus(values, 3);
      auto v4 = h.IterPlus(values, 4);
      auto v5 = h.IterPlus(values, 5);
      auto v6 = h.IterPlus(values, 6);
      auto v7 = h.IterPlus(values, 7);

      auto p0 = h.IterPlus(p, c0);
      auto p1 = h.IterPlus(p, c1);
      auto p2 = h.IterPlus(p, c2);
      auto p3 = h.IterPlus(p, c3);
      auto p4 = h.IterPlus(p, c4);
      auto p5 = h.IterPlus(p, c5);
      auto p6 = h.IterPlus(p, c6);
      auto p7 = h.IterPlus(p, c7);

      h.Add(v0, p0);
      h.Add(v1, p1);
      h.Add(v2, p2);
      h.Add(v3, p3);
      h.Add(v4, p4);
      h.Add(v5, p5);
      h.Add(v6, p6);
      h.Add(v7, p7);
    }
  }

  if (dt == DenseType::GF128) {
    const uint128_t* __restrict p2 = h.IterPlus(p, sparse_size);

    std::array<uint128_t, 32> xx;
    memcpy(xx.data(), dense_span.data(), sizeof(uint128_t) * 32);

    for (uint64_t k = 0; k < 4; ++k) {
      uint128_t* __restrict values = h.IterPlus(values_span.data(), k * 8);
      auto x = xx.data() + k * 8;

      h.MultAdd(h.IterPlus(values, 0), p2, x[0]);
      h.MultAdd(h.IterPlus(values, 1), p2, x[1]);
      h.MultAdd(h.IterPlus(values, 2), p2, x[2]);
      h.MultAdd(h.IterPlus(values, 3), p2, x[3]);
      h.MultAdd(h.IterPlus(values, 4), p2, x[4]);
      h.MultAdd(h.IterPlus(values, 5), p2, x[5]);
      h.MultAdd(h.IterPlus(values, 6), p2, x[6]);
      h.MultAdd(h.IterPlus(values, 7), p2, x[7]);
    }

    for (uint64_t i = 1; i < dense_size; ++i) {
      p2 = h.IterPlus(p2, 1);

      for (uint64_t k = 0; k < 4; ++k) {
        auto x = xx.data() + k * 8;
        auto dense = dense_span.data() + k * 8;
        uint128_t* __restrict values = h.IterPlus(values_span.data(), k * 8);

        x[0] = (Galois128(x[0]) * dense[0]).get<uint128_t>(0);
        x[1] = (Galois128(x[1]) * dense[1]).get<uint128_t>(0);
        x[2] = (Galois128(x[2]) * dense[2]).get<uint128_t>(0);
        x[3] = (Galois128(x[3]) * dense[3]).get<uint128_t>(0);
        x[4] = (Galois128(x[4]) * dense[4]).get<uint128_t>(0);
        x[5] = (Galois128(x[5]) * dense[5]).get<uint128_t>(0);
        x[6] = (Galois128(x[6]) * dense[6]).get<uint128_t>(0);
        x[7] = (Galois128(x[7]) * dense[7]).get<uint128_t>(0);

        h.MultAdd(h.IterPlus(values, 0), p2, x[0]);
        h.MultAdd(h.IterPlus(values, 1), p2, x[1]);
        h.MultAdd(h.IterPlus(values, 2), p2, x[2]);
        h.MultAdd(h.IterPlus(values, 3), p2, x[3]);
        h.MultAdd(h.IterPlus(values, 4), p2, x[4]);
        h.MultAdd(h.IterPlus(values, 5), p2, x[5]);
        h.MultAdd(h.IterPlus(values, 6), p2, x[6]);
        h.MultAdd(h.IterPlus(values, 7), p2, x[7]);
      }
    }
  } else {
    std::array<uint64_t, 8> d2;
    std::array<uint8_t, 8> b;

    for (uint64_t k = 0; k < 4; ++k) {
      auto values = h.IterPlus(values_span.data(), k * 8);
      auto dense = dense_span.data() + k * 8;

      YACL_ENFORCE(dense_size <= 64);
      for (uint64_t i = 0; i < 8; ++i) {
        d2[i] = yacl::DecomposeUInt128(dense[i]).first;
      }

      for (uint64_t i = 0; i < dense_size; ++i) {
        b[0] = d2[0] & 1;
        b[1] = d2[1] & 1;
        b[2] = d2[2] & 1;
        b[3] = d2[3] & 1;
        b[4] = d2[4] & 1;
        b[5] = d2[5] & 1;
        b[6] = d2[6] & 1;
        b[7] = d2[7] & 1;

        d2[0] = d2[0] >> 1;
        d2[1] = d2[1] >> 1;
        d2[2] = d2[2] >> 1;
        d2[3] = d2[3] >> 1;
        d2[4] = d2[4] >> 1;
        d2[5] = d2[5] >> 1;
        d2[6] = d2[6] >> 1;
        d2[7] = d2[7] >> 1;

        auto p2 = h.IterPlus(p, sparse_size + i);

        h.MultAdd(h.IterPlus(values, 0), p2, b[0]);
        h.MultAdd(h.IterPlus(values, 1), p2, b[1]);
        h.MultAdd(h.IterPlus(values, 2), p2, b[2]);
        h.MultAdd(h.IterPlus(values, 3), p2, b[3]);
        h.MultAdd(h.IterPlus(values, 4), p2, b[4]);
        h.MultAdd(h.IterPlus(values, 5), p2, b[5]);
        h.MultAdd(h.IterPlus(values, 6), p2, b[6]);
        h.MultAdd(h.IterPlus(values, 7), p2, b[7]);
      }
    }
  }
}

// decodes one instances. rows should contain the row indicies, dense the
// dense part. values is where the values are written to. p is the Paxos, h is
// the value op. helper.
template <typename IdxType>
void Paxos<IdxType>::Decode1(absl::Span<IdxType> rows, const uint128_t dense,
                             uint128_t* values, const PxVector& p,
                             const PxVector::Helper& h) {
  h.Assign(values, p[rows[0]]);
  for (uint64_t j = 1; j < weight; ++j) {
    h.Add(values, p[rows[j]]);
  }

  SPDLOG_DEBUG("mSparseSize:{}, mDenseSize:{}, p.size():{}", sparse_size,
               dense_size, p.size());

  if (dt == DenseType::GF128) {
    uint128_t x = dense;
    h.MultAdd(values, p[sparse_size], x);

    for (uint64_t i = 1; i < dense_size; ++i) {
      x = (Galois128(x) * dense).get<uint128_t>(0);
      h.MultAdd(values, p[i + sparse_size], x);
    }
  } else {
    for (uint64_t i = 0; i < dense_size; ++i) {
      if (*BitIterator((uint8_t*)(&dense), i)) {
        h.Add(values, p[i + sparse_size]);
      }
    }
  }
}

template class Paxos<uint64_t>;
template class Paxos<uint32_t>;
template class Paxos<uint16_t>;
template class Paxos<uint8_t>;

}  // namespace psi::rr22::okvs
