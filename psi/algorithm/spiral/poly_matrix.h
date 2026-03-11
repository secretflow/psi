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

#pragma once

#include <memory>

#include "absl/types/span.h"
#include "seal/modulus.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/params.h"

#include "psi/algorithm/spiral/serializable.pb.h"

namespace psi::spiral {

// forward declaration
class PolyMatrixNtt;

class PolyMatrixRaw {
 public:
  PolyMatrixRaw(size_t poly_len, size_t rows, size_t cols,
                std::vector<uint64_t> data)
      : poly_len_(poly_len), rows_(rows), cols_(cols) {
    YACL_ENFORCE(poly_len * rows * cols <= data.size());
    data_ = std::move(data);
  }

  // zero matrix
  PolyMatrixRaw(size_t poly_len, size_t rows, size_t cols)
      : poly_len_(poly_len),
        rows_(rows),
        cols_(cols),
        data_(rows * cols * poly_len) {}

  PolyMatrixRaw() = default;

  PolyMatrixRaw(const PolyMatrixRaw& other) {
    if (&other != this) {
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = other.data_;
    }
  }

  PolyMatrixRaw& operator=(const PolyMatrixRaw& other) {
    if (this != &other) {
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = other.data_;
    }

    return *this;
  }

  PolyMatrixRaw(PolyMatrixRaw&& other) noexcept {
    if (this != &other) {
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = std::move(other.data_);
    }
  }

  PolyMatrixRaw& operator=(PolyMatrixRaw&& other) noexcept {
    if (this != &other) {
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = std::move(other.data_);
    }
    return *this;
  }

  bool operator==(const PolyMatrixRaw& other) const {
    return poly_len_ == other.poly_len_ && rows_ == other.rows_ &&
           cols_ == other.cols_ && data_ == other.data_;
  }

  bool operator!=(const PolyMatrixRaw& other) const {
    return !(*this == other);
  }

  bool IsNtt() const { return false; };

  size_t Rows() const { return rows_; }

  size_t Cols() const { return cols_; }

  size_t Rows() { return rows_; }

  size_t Cols() { return cols_; }

  size_t PolyLen() const { return poly_len_; }

  size_t PolyLen() { return poly_len_; }

  size_t NumWords() const { return poly_len_; }
  size_t NumWords() { return poly_len_; }

  size_t PolyStartIndex(size_t row, size_t col) const {
    return (row * cols_ + col) * NumWords();
  }

  absl::Span<uint64_t> Data() { return absl::MakeSpan(data_); }

  absl::Span<const uint64_t> Data() const { return absl::MakeSpan(data_); }

  void SetData(std::vector<uint64_t>& data) {
    YACL_ENFORCE_EQ(rows_ * cols_ * NumWords(), data.size());
    std::memcpy(data_.data(), data.data(), sizeof(uint64_t) * data.size());
  }

  // PolyMatrix operators
  void CopyInto(const PolyMatrixRaw& p, size_t target_row, size_t target_col);

  PolyMatrixRaw SubMatrix(size_t target_row, size_t target_col, size_t rows,
                          size_t cols) const;

  PolyMatrixRaw PadTop(size_t pad_rows);

  absl::Span<uint64_t> Poly(size_t r, size_t c);
  absl::Span<const uint64_t> Poly(size_t r, size_t c) const;

  void ReduceMod(uint64_t modulus);
  void ReduceMod(const seal::Modulus& modulus);

  void Reset(std::shared_ptr<Params> params, size_t rows, size_t cols);
  void Reset(size_t poly_len, size_t rows, size_t cols);

  void Rescale(size_t start_row, size_t end_row, uint64_t in_modulus,
               uint64_t out_modulus);

  PolyMatrixRaw operator-() = delete;

  // seralize related methods
  PolyMatrixProto ToProto() const;
  PolyMatrixProto ToProtoRng() const;

  static PolyMatrixRaw FromProto(const PolyMatrixProto& proto,
                                 const Params& params);
  static PolyMatrixRaw FromProtoRng(const PolyMatrixProto& proto,
                                    const Params& params,
                                    yacl::crypto::Prg<uint64_t>& rng);

  // static methods without params
  static PolyMatrixRaw Zero(size_t poly_len, size_t rows, size_t cols) {
    return PolyMatrixRaw(poly_len, rows, cols);
  }

  static PolyMatrixRaw Identity(size_t poly_len, size_t rows, size_t cols);

  static PolyMatrixRaw SingleValue(size_t poly_len, uint64_t value);

  static PolyMatrixRaw Random(const Params& params, size_t rows, size_t cols);

  static PolyMatrixRaw RandomPrg(const Params& params, size_t rows, size_t cols,
                                 yacl::crypto::Prg<uint64_t>& prg);
  static PolyMatrixRaw Recover(const Params& params, uint64_t q_1, uint64_t q_2,
                               const std::vector<uint8_t>& ciphertext);

 private:
  size_t poly_len_ = 0;
  size_t rows_ = 0;
  size_t cols_ = 0;
  std::vector<std::uint64_t> data_;
};

class PolyMatrixNtt {
 public:
  PolyMatrixNtt(size_t crt_count, size_t poly_len, size_t rows, size_t cols,
                std::vector<uint64_t>&& data)
      : crt_count_(crt_count), poly_len_(poly_len), rows_(rows), cols_(cols) {
    WEAK_ENFORCE(poly_len * crt_count * rows * cols <= data.size());
    data_ = std::move(data);
  }

  PolyMatrixNtt(size_t crt_count, size_t poly_len, size_t rows, size_t cols)
      : crt_count_(crt_count),
        poly_len_(poly_len),
        rows_(rows),
        cols_(cols),
        data_(rows * cols * poly_len * crt_count) {}

  PolyMatrixNtt() = default;

  PolyMatrixNtt(const PolyMatrixNtt& other) {
    if (this != &other) {
      crt_count_ = other.crt_count_;
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = other.data_;
    }
  }

  PolyMatrixNtt& operator=(const PolyMatrixNtt& other) {
    if (this != &other) {
      crt_count_ = other.crt_count_;
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = other.data_;
    }
    return *this;
  }

  PolyMatrixNtt(PolyMatrixNtt&& other) noexcept {
    if (this != &other) {
      crt_count_ = other.crt_count_;
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = std::move(other.data_);
    }
  }

  PolyMatrixNtt& operator=(PolyMatrixNtt&& other) noexcept {
    if (this != &other) {
      crt_count_ = other.crt_count_;
      poly_len_ = other.poly_len_;
      rows_ = other.rows_;
      cols_ = other.cols_;
      data_ = std::move(other.data_);
    }
    return *this;
  }

  bool operator==(const PolyMatrixNtt& other) const {
    return crt_count_ == other.crt_count_ && rows_ == other.rows_ &&
           cols_ == other.cols_ && data_ == other.data_;
  }

  bool operator!=(const PolyMatrixNtt& other) const {
    return !(*this == other);
  }

  void SetData(std::vector<uint64_t>& data) {
    YACL_ENFORCE_EQ(rows_ * cols_ * NumWords(), data.size());
    std::memcpy(data_.data(), data.data(), sizeof(uint64_t) * data.size());
  }

  PolyMatrixNtt operator*(const PolyMatrixNtt& other) = delete;

  PolyMatrixNtt operator+(const PolyMatrixNtt& other) = delete;

  bool IsNtt() const { return true; };
  size_t Rows() const { return rows_; }
  size_t Cols() const { return cols_; }
  size_t Rows() { return rows_; }
  size_t Cols() { return cols_; }

  size_t NumWords() const { return crt_count_ * poly_len_; };

  size_t PolyStartIndex(size_t row, size_t col) const {
    return (row * cols_ + col) * NumWords();
  }

  absl::Span<uint64_t> Data() { return absl::MakeSpan(data_); }

  absl::Span<const uint64_t> Data() const { return absl::MakeSpan(data_); }

  void CopyInto(const PolyMatrixNtt& p, size_t target_row, size_t target_col);

  PolyMatrixNtt SubMatrix(size_t target_row, size_t target_col, size_t rows,
                          size_t cols) const;

  PolyMatrixNtt PadTop(size_t pad_rows);

  absl::Span<uint64_t> Poly(size_t r, size_t c);

  absl::Span<const uint64_t> Poly(size_t r, size_t c) const;

  void Reset(std::shared_ptr<Params> params, size_t rows, size_t cols);

  // static methdos without params
  static PolyMatrixNtt Zero(size_t crt_count, size_t poly_len, size_t rows,
                            size_t cols) {
    return PolyMatrixNtt(crt_count, poly_len, rows, cols);
  }

  static PolyMatrixNtt Random(const Params& params, size_t rows, size_t cols);

  static PolyMatrixNtt RandomPrg(const Params& params, size_t rows, size_t cols,
                                 yacl::crypto::Prg<uint64_t>& prg);

  // static methods
  static PolyMatrixNtt Zero(std::shared_ptr<Params> params, size_t rows,
                            size_t cols);

  static PolyMatrixNtt Random(std::shared_ptr<Params> params, size_t rows,
                              size_t cols);

  static PolyMatrixNtt RandomPrg(std::shared_ptr<Params> params, size_t rows,
                                 size_t cols, yacl::crypto::Prg<uint64_t>& prg);

  size_t CrtCount() { return crt_count_; }
  size_t PolyLen() { return poly_len_; }

  size_t CrtCount() const { return crt_count_; }
  size_t PolyLen() const { return poly_len_; }

 private:
  size_t crt_count_ = 0;
  size_t poly_len_ = 0;
  size_t rows_ = 0;
  size_t cols_ = 0;
  std::vector<std::uint64_t> data_;
};

//----some utils method for PolyMatrix-----

// Poly operators

// res = a * b
void MultiplyPoly(const Params& params, absl::Span<uint64_t> res,
                  absl::Span<const uint64_t> a, absl::Span<const uint64_t> b);

// res += (a * b)
void MultiplyAddPoly(const Params& params, absl::Span<uint64_t> res,
                     absl::Span<const uint64_t> a,
                     absl::Span<const uint64_t> b);
// res = a + b
void AddPoly(const Params& params, absl::Span<uint64_t> res,
             absl::Span<const uint64_t> a, absl::Span<const uint64_t> b);
// res += a
void AddPolyInto(const Params& params, absl::Span<const uint64_t> res,
                 absl::Span<const uint64_t> a);
// res = -a
void NegatePoly(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> a);

void AutomotphPoly(const Params& params, absl::Span<uint64_t> res,
                   absl::Span<const uint64_t> a, size_t t);

// in is a PolyRaw, res is a RNS
void ReduceCopy(const Params& params, absl::Span<uint64_t> res,
                absl::Span<const uint64_t> in);

void ReducePoly(const Params& params, absl::Span<uint64_t> res);

// matrix operator

PolyMatrixNtt ShiftRowsByOne(const PolyMatrixNtt& in);

PolyMatrixRaw Stack(const PolyMatrixRaw& a, const PolyMatrixRaw& b);
PolyMatrixNtt StackNtt(const PolyMatrixNtt& a, const PolyMatrixNtt& b);

// a is a 1x1 matrix, view as a scalar
void ScalarMultiply(const Params& params, PolyMatrixNtt& res,
                    const PolyMatrixNtt& a, const PolyMatrixNtt& b);
PolyMatrixNtt ScalarMultiply(const Params& params, const PolyMatrixNtt& a,
                             const PolyMatrixNtt& b);

void Multiply(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
              const PolyMatrixNtt& b);

PolyMatrixNtt Multiply(const Params& params, const PolyMatrixNtt& a,
                       const PolyMatrixNtt& b);
void MultiplyNoReduce(PolyMatrixNtt& res, const PolyMatrixNtt& a,
                      const PolyMatrixNtt& b, size_t start_inner_dim);

void Automorphism(const Params& params, PolyMatrixRaw& res,
                  const PolyMatrixRaw& a, size_t t);
PolyMatrixRaw Automorphism(const Params& params, const PolyMatrixRaw& a,
                           size_t t);
// res = a + b
void Add(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
         const PolyMatrixNtt& b);
PolyMatrixNtt Add(const Params& params, const PolyMatrixNtt& a,
                  const PolyMatrixNtt& b);

// res += a
void AddInto(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a);
void AddIntoAt(const Params& params, PolyMatrixNtt& res, const PolyMatrixNtt& a,
               size_t t_row, size_t t_col);

void Negate(const Params& params, PolyMatrixRaw& res, const PolyMatrixRaw& a);
PolyMatrixRaw Negate(const Params& params, const PolyMatrixRaw& a);

void FromNtt(const Params& params, PolyMatrixRaw& out, const PolyMatrixNtt& in);
PolyMatrixRaw FromNtt(const Params& params, const PolyMatrixNtt& in);
void FromNttScratch(const Params& params, PolyMatrixRaw& out,
                    absl::Span<uint64_t> scratch, const PolyMatrixNtt& in);

void ToNtt(const Params& params, PolyMatrixNtt& out, const PolyMatrixRaw& in);
PolyMatrixNtt ToNtt(const Params& params, const PolyMatrixRaw& in);
void ToNttNoReduce(const Params& params, PolyMatrixNtt& out,
                   const PolyMatrixRaw& in);

PolyMatrixRaw MatrixWithIdentity(const PolyMatrixRaw& p);

}  // namespace psi::spiral
