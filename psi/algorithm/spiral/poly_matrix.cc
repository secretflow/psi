// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/algorithm/spiral/poly_matrix.h"

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include "absl/types/span.h"
#include "seal/modulus.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/rand/rand.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/arith/ntt.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

//-------PolyMatrixRaw

PolyMatrixProto PolyMatrixRaw::ToProto() const {
  PolyMatrixProto proto;
  proto.set_rows(rows_);
  proto.set_cols(cols_);

  proto.mutable_data()->Reserve(data_.size());
  proto.mutable_data()->Assign(data_.begin(), data_.end());
  return proto;
}

PolyMatrixProto PolyMatrixRaw::ToProtoRng() const {
  WEAK_ENFORCE(rows_ > 1);

  PolyMatrixProto proto;
  proto.set_rows(rows_ - 1);
  proto.set_cols(cols_);
  // skip the first row
  size_t offset = cols_ * poly_len_;
  proto.mutable_data()->Reserve(data_.size() - offset);
  proto.mutable_data()->Assign(data_.begin() + offset, data_.end());
  return proto;
}

PolyMatrixRaw PolyMatrixRaw::FromProto(const PolyMatrixProto& proto,
                                       const Params& params) {
  size_t rows = proto.rows();
  size_t cols = proto.cols();

  std::vector<uint64_t> data;
  data.reserve(proto.data_size());
  for (const auto& value : proto.data()) {
    data.push_back(value);
  }

  YACL_ENFORCE_EQ(rows * cols * params.PolyLen(), data.size());

  return PolyMatrixRaw(params.PolyLen(), rows, cols, std::move(data));
}

PolyMatrixRaw PolyMatrixRaw::FromProtoRng(const PolyMatrixProto& proto,
                                          const Params& params,
                                          yacl::crypto::Prg<uint64_t>& rng) {
  size_t rows = proto.rows();
  size_t cols = proto.cols();

  YACL_ENFORCE_EQ(rows * cols * params.PolyLen(),
                  static_cast<size_t>(proto.data_size()));

  std::vector<uint64_t> data;
  size_t first_row_coeffs = cols * params.PolyLen();
  size_t rest_row_coeffs = proto.data_size();
  data.reserve(first_row_coeffs + rest_row_coeffs);

  // resconstruct the first row by rng
  for (size_t i = 0; i < first_row_coeffs; ++i) {
    data.push_back(params.Modulus() - (rng() % params.Modulus()));
  }
  // rest rows
  for (const auto& value : proto.data()) {
    data.push_back(value);
  }
  return PolyMatrixRaw(params.PolyLen(), rows + 1, cols, std::move(data));
}

// PolyMatrixRaw

void PolyMatrixRaw::CopyInto(const PolyMatrixRaw& p, size_t target_row,
                             size_t target_col) {
  WEAK_ENFORCE(target_row < rows_);
  WEAK_ENFORCE(target_col < cols_);
  WEAK_ENFORCE(target_row + p.Rows() <= rows_);
  WEAK_ENFORCE(target_col + p.Cols() <= cols_);
  WEAK_ENFORCE(p.NumWords() == this->NumWords());

  // copy each poly
  for (size_t r = 0; r < p.Rows(); ++r) {
    for (size_t c = 0; c < p.Cols(); ++c) {
      size_t src_idx = p.PolyStartIndex(r, c);
      size_t dest_idx = this->PolyStartIndex(target_row + r, target_col + c);
      std::memcpy(data_.data() + dest_idx, p.Data().data() + src_idx,
                  NumWords() * sizeof(uint64_t));
    }
  }
}

PolyMatrixRaw PolyMatrixRaw::SubMatrix(size_t target_row, size_t target_col,
                                       size_t rows, size_t cols) const {
  WEAK_ENFORCE(target_row < rows_);
  WEAK_ENFORCE(target_col < cols_);
  WEAK_ENFORCE(target_row + rows <= rows_);
  WEAK_ENFORCE(target_col + cols <= cols_);

  PolyMatrixRaw sub_matrix = PolyMatrixRaw::Zero(poly_len_, rows, cols);
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      size_t src_idx = this->PolyStartIndex(target_row + r, target_col + c);
      size_t dest_idx = sub_matrix.PolyStartIndex(r, c);
      // copy each poly
      std::memcpy(sub_matrix.data_.data() + dest_idx, data_.data() + src_idx,
                  NumWords() * sizeof(uint64_t));
    }
  }
  return sub_matrix;
}

PolyMatrixRaw PolyMatrixRaw::PadTop(size_t pad_rows) {
  auto padded = PolyMatrixRaw::Zero(poly_len_, rows_ + pad_rows, cols_);
  padded.CopyInto(*this, pad_rows, 0);
  return padded;
}

void PolyMatrixRaw::ReduceMod(uint64_t modulus) {
  WEAK_ENFORCE(modulus > 0);
  for (size_t r = 0; r < rows_; ++r) {
    for (size_t c = 0; c < cols_; ++c) {
      auto poly_idx = PolyStartIndex(r, c);
      for (size_t z = 0; z < NumWords(); z++) {
        data_[poly_idx + z] %= modulus;
      }
    }
  }
}

void PolyMatrixRaw::ReduceMod(const seal::Modulus& modulus) {
  for (size_t r = 0; r < rows_; ++r) {
    for (size_t c = 0; c < cols_; ++c) {
      auto poly_idx = PolyStartIndex(r, c);
      for (size_t z = 0; z < NumWords(); z++) {
        data_[poly_idx + z] = arith::BarrettRawU64(
            data_[poly_idx + z], modulus.const_ratio()[1], modulus.value());
      }
    }
  }
}

absl::Span<uint64_t> PolyMatrixRaw::Poly(size_t r, size_t c) {
  auto start_idx = PolyStartIndex(r, c);
  return absl::MakeSpan(data_.data() + start_idx, NumWords());
}

absl::Span<const uint64_t> PolyMatrixRaw::Poly(size_t r, size_t c) const {
  auto start_idx = PolyStartIndex(r, c);
  return absl::MakeSpan(data_.data() + start_idx, NumWords());
}

void PolyMatrixRaw::Rescale(size_t start_row, size_t end_row,
                            uint64_t in_modulus, uint64_t out_modulus) {
  WEAK_ENFORCE(start_row < end_row);
  WEAK_ENFORCE(end_row <= rows_);

  for (size_t r = start_row; r < end_row; ++r) {
    for (size_t c = 0; c < cols_; ++c) {
      size_t poly_idx = PolyStartIndex(r, c);
      for (size_t i = 0; i < NumWords(); ++i) {
        data_[poly_idx + i] =
            arith::Rescale(data_[poly_idx + i], in_modulus, out_modulus);
      }
    }
  }
}

void PolyMatrixRaw::Reset(size_t poly_len, size_t rows, size_t cols) {
  poly_len_ = poly_len;
  rows_ = rows;
  cols_ = cols;
  data_.resize(poly_len * rows * cols);
  std::fill(data_.begin(), data_.end(), 0);
}

PolyMatrixRaw PolyMatrixRaw::Identity(size_t poly_len, size_t rows,
                                      size_t cols) {
  WEAK_ENFORCE(rows == cols);

  size_t num_coeffs = rows * cols * poly_len;
  std::vector<uint64_t> data(num_coeffs);
  for (size_t r = 0; r < rows; ++r) {
    size_t c = r;
    size_t idx = r * cols * poly_len + c * poly_len;
    // set the poly at (r, r) to be 1
    data[idx] = 1ULL;
  }
  return PolyMatrixRaw(poly_len, rows, cols, std::move(data));
}

PolyMatrixRaw PolyMatrixRaw::SingleValue(size_t poly_len, uint64_t value) {
  PolyMatrixRaw out = PolyMatrixRaw::Zero(poly_len, 1, 1);
  out.data_[0] = value;
  return out;
}

PolyMatrixRaw PolyMatrixRaw::Random(const Params& params, size_t rows,
                                    size_t cols) {
  yacl::crypto::Prg<uint64_t> prg(yacl::MakeUint128(0, 20001));
  return RandomPrg(params, rows, cols, prg);
}

PolyMatrixRaw PolyMatrixRaw::RandomPrg(const Params& params, size_t rows,
                                       size_t cols,
                                       yacl::crypto::Prg<uint64_t>& prg) {
  PolyMatrixRaw out = PolyMatrixRaw::Zero(params.PolyLen(), rows, cols);
  prg.Fill(out.Data());
  out.ReduceMod(params.ModulusSeal());
  return out;
}

PolyMatrixRaw PolyMatrixRaw::Recover(const Params& params, uint64_t q_1,
                                     uint64_t q_2,
                                     const std::vector<uint8_t>& ciphertext) {
  const size_t q_1_bits =
      static_cast<size_t>(std::ceil(std::log2(static_cast<double>(q_2))));
  const size_t q_2_bits =
      static_cast<size_t>(std::ceil(std::log2(static_cast<double>(q_1))));

  const size_t total_sz_bits = (q_1_bits + q_2_bits) * params.PolyLen();
  const size_t total_sz_bytes = (total_sz_bits + 7) / 8;
  assert(ciphertext.size() == total_sz_bytes && "Ciphertext size mismatch");

  PolyMatrixRaw res = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
  size_t bit_offs = 0;

  uint64_t* row_0_ptr = res.Data().data();
  uint64_t* row_1_ptr = res.Data().data() + params.PolyLen();

  for (size_t z = 0; z < params.PolyLen(); ++z) {
    uint64_t val = util::ReadArbitraryBits(ciphertext, bit_offs, q_1_bits);
    row_0_ptr[z] = arith::Rescale(val, q_2, params.Modulus());
    bit_offs += q_1_bits;
  }

  for (size_t z = 0; z < params.PolyLen(); ++z) {
    uint64_t val = util::ReadArbitraryBits(ciphertext, bit_offs, q_2_bits);
    row_1_ptr[z] = arith::Rescale(val, q_1, params.Modulus());
    bit_offs += q_2_bits;
  }

  return res;
}

void PolyMatrixNtt::CopyInto(const PolyMatrixNtt& p, size_t target_row,
                             size_t target_col) {
  WEAK_ENFORCE(target_row < rows_);
  WEAK_ENFORCE(target_col < cols_);
  WEAK_ENFORCE(target_row + p.Rows() <= rows_,
               "target_row: {}, p.Rows: {}, rows: {}", target_row, p.Rows(),
               rows_);
  WEAK_ENFORCE(target_col + p.Cols() <= cols_);
  WEAK_ENFORCE(p.NumWords() == this->NumWords());
  // copy each poly
  for (size_t r = 0; r < p.Rows(); ++r) {
    for (size_t c = 0; c < p.Cols(); ++c) {
      size_t src_idx = p.PolyStartIndex(r, c);
      size_t dest_idx = this->PolyStartIndex(target_row + r, target_col + c);
      std::memcpy(data_.data() + dest_idx, p.Data().data() + src_idx,
                  NumWords() * sizeof(uint64_t));
    }
  }
}

PolyMatrixNtt PolyMatrixNtt::SubMatrix(size_t target_row, size_t target_col,
                                       size_t rows, size_t cols) const {
  WEAK_ENFORCE(target_row < rows_);
  WEAK_ENFORCE(target_col < cols_);
  WEAK_ENFORCE(target_row + rows <= rows_);
  WEAK_ENFORCE(target_col + cols <= cols_);

  PolyMatrixNtt sub_matrix =
      PolyMatrixNtt::Zero(crt_count_, poly_len_, rows, cols);
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      size_t src_idx = this->PolyStartIndex(target_row + r, target_col + c);
      size_t dest_idx = sub_matrix.PolyStartIndex(r, c);
      std::memcpy(sub_matrix.data_.data() + dest_idx, data_.data() + src_idx,
                  NumWords() * sizeof(uint64_t));
    }
  }
  return sub_matrix;
}

PolyMatrixNtt PolyMatrixNtt::PadTop(size_t pad_rows) {
  auto padded =
      PolyMatrixNtt::Zero(crt_count_, poly_len_, rows_ + pad_rows, cols_);
  padded.CopyInto(*this, pad_rows, 0);
  return padded;
}

absl::Span<uint64_t> PolyMatrixNtt::Poly(size_t r, size_t c) {
  auto start_idx = PolyStartIndex(r, c);
  return absl::MakeSpan(data_.data() + start_idx, NumWords());
}

absl::Span<const uint64_t> PolyMatrixNtt::Poly(size_t r, size_t c) const {
  auto start_idx = PolyStartIndex(r, c);
  return absl::MakeSpan(data_.data() + start_idx, NumWords());
}

PolyMatrixNtt PolyMatrixNtt::Random(const Params& params, size_t rows,
                                    size_t cols) {
  yacl::crypto::Prg<uint64_t> prg(yacl::MakeUint128(0, 20002));
  return RandomPrg(params, rows, cols, prg);
}

PolyMatrixNtt PolyMatrixNtt::RandomPrg(const Params& params, size_t rows,
                                       size_t cols,
                                       yacl::crypto::Prg<uint64_t>& prg) {
  PolyMatrixNtt out =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), rows, cols);
  prg.Fill(out.Data());
  // reduce
  for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
      auto poly_idx = out.PolyStartIndex(r, c);
      for (size_t i = 0; i < params.CrtCount(); ++i) {
        for (size_t j = 0; j < params.PolyLen(); ++j) {
          auto ele_idx =
              util::CalcIndex({i, j}, {params.CrtCount(), params.PolyLen()});
          out.data_[poly_idx + ele_idx] =
              arith::BarrettRawU64(out.data_[poly_idx + ele_idx],
                                   params.BarrettCr1(i), params.Moduli(i));
        }
      }
    }
  }

  return out;
}

}  // namespace psi::spiral
