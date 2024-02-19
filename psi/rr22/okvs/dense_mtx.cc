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

#include "psi/rr22/okvs/dense_mtx.h"

#include <utility>

namespace psi::rr22::okvs {

void DenseMtx::Row::swap(const Row& r) {
  YACL_ENFORCE(mtx.cols() == r.mtx.cols());

  for (uint64_t col_idx = 0; col_idx < mtx.cols(); ++col_idx) {
    uint8_t bit = r.mtx(r.idx, col_idx);
    r.mtx(r.idx, col_idx) = mtx(idx, col_idx);
    mtx(idx, col_idx) = bit;
  }
}

// returns true if the row is zero.
bool DenseMtx::Row::IsZero() const {
  for (uint64_t col_idx = 0; col_idx < mtx.cols(); ++col_idx) {
    uint8_t bit = mtx(idx, col_idx);
    if (bit) {
      return false;
    }
  }
  return true;
}

void DenseMtx::Row::operator^=(const Row& r) {
  for (uint64_t col_idx = 0; col_idx < mtx.cols(); ++col_idx) {
    mtx(idx, col_idx) ^= r.mtx(r.idx, col_idx);
  }
}

void DenseMtx::resize(uint64_t rows, uint64_t cols) {
  bit_rows_ = rows;
  rows_ = (rows_ + 127) / 128;
  cols_ = cols;
  data_.resize(cols_ * rows_);
  data_mtx_ = MatrixView<uint128_t>(data_.data(), cols_, rows_);
}

DenseMtx DenseMtx::Identity(uint64_t n) {
  DenseMtx I(n, n);

  for (uint64_t i = 0; i < n; ++i) {
    I(i, i) = 1;
  }

  return I;
}

// multiply this matrix by m.

DenseMtx DenseMtx::Mult(const DenseMtx& m) {
  YACL_ENFORCE(cols() == m.rows());

  DenseMtx ret(rows(), m.cols());

  for (uint64_t i = 0; i < ret.rows(); ++i) {
    for (uint64_t j = 0; j < ret.cols(); ++j) {
      uint8_t v = 0;
      for (uint64_t k = 0; k < cols(); ++k) {
        v = v ^ ((*this)(i, k) & m(k, j));
      }

      ret(i, j) = v;
    }
  }

  return ret;
}

DenseMtx DenseMtx::Add(DenseMtx& m) {
  YACL_ENFORCE(rows() == m.rows() && cols() == m.cols());

  auto ret = *this;
  for (uint64_t i = 0; i < data_.size(); ++i)
    ret.data_mtx_(i) = ret.data_mtx_(i) ^ m.data_mtx_(i);

  return ret;
}

DenseMtx DenseMtx::Invert() const {
  YACL_ENFORCE(rows() == cols());

  auto mtx = *this;
  auto n = this->rows();

  auto Inv = Identity(n);

  for (uint64_t i = 0; i < n; ++i) {
    if (mtx(i, i) == 0) {
      for (uint64_t j = i + 1; j < n; ++j) {
        if (mtx(j, i) == 1) {
          mtx.row(i).swap(mtx.row(j));
          Inv.row(i).swap(Inv.row(j));
          break;
        }
      }

      if (mtx(i, i) == 0) {
        // std::cout << mtx << std::endl;
        return {};
      }
    }

    for (uint64_t j = 0; j < n; ++j) {
      if (j != i && mtx(j, i)) {
        for (uint64_t k = 0; k < n; ++k) {
          mtx(j, k) ^= mtx(i, k);
          Inv(j, k) ^= Inv(i, k);
        }
      }
    }
  }

  return Inv;
}

}  // namespace psi::rr22::okvs
