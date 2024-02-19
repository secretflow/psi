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

#include <utility>
#include <vector>

#include "yacl/base/exception.h"

#include "psi/rr22/okvs/galois128.h"

namespace psi::rr22::okvs {

// A class to reference a specific bit.
class BitReference {
 public:
  // Default copy constructor
  BitReference(const BitReference& rhs) = default;

  // Construct a reference to the bit in the provided byte offset by the shift.
  BitReference(uint8_t* byte, uint64_t shift)
      : bytes_(byte + (shift / 8)), shift_(shift % 8) {}

  // Construct a reference to the bit in the provided byte offset by the shift
  // and mask. Shift should be less than 8. and the mask should equal 1 <<
  // shift.
  BitReference(uint8_t* byte, [[maybe_unused]] uint8_t mask, uint8_t shift)
      : BitReference(byte, shift) {}

  // Copy the underlying values of the rhs to the lhs.
  void operator=(const BitReference& rhs) { *this = (uint8_t)rhs; }

  // Copy the value of the rhs to the lhs.
  inline void operator=(uint8_t n) {
    bool b = n;
    *bytes_ ^= (*bytes_ ^ ((b & 1) << shift_)) & (1 << shift_);
  }

  inline void operator^=(bool b) { *bytes_ ^= ((b & 1) << shift_); }

  // Convert the reference to the underlying value
  operator uint8_t() const { return (*bytes_ >> shift_) & 1; }

 private:
  uint8_t* bytes_;
  uint8_t shift_;
};

// Function to allow the printing of a BitReference.
inline std::ostream& operator<<(std::ostream& out, const BitReference& bit) {
  out << (uint32_t)bit;
  return out;
}

// A class to allow the iteration of bits.
class BitIterator {
 public:
  // Would be random access with some extra methods.
  typedef std::bidirectional_iterator_tag iterator_category;
  typedef ptrdiff_t difference_type;
  typedef uint8_t value_type;
  typedef BitReference reference;
  typedef void pointer;  // Can't support operator->

  BitIterator() = default;
  // Default copy constructor
  BitIterator(const BitIterator& cp) = default;

  // Construct a reference to the bit in the provided byte offset by the shift.
  // Shift should be less than 8.
  explicit BitIterator(uint8_t* byte, uint64_t shift = 0)
      : bytes_(byte + (shift / 8)), shift_(shift & 7) {}

  // Construct a reference to the current bit pointed to by the iterator.
  BitReference operator*() { return BitReference(bytes_, shift_); }

  // Pre increment the iterator by 1.
  BitIterator& operator++() {
    bytes_ += (shift_ == 7) & 1;
    ++shift_ &= 7;
    return *this;
  }

  // Post increment the iterator by 1. Returns a copy of this class.
  BitIterator operator++(int) {
    BitIterator ret(*this);

    bytes_ += (shift_ == 7) & 1;
    ++shift_ &= 7;

    return ret;
  }

  // Pre decrement the iterator by 1.
  BitIterator& operator--() {
    bytes_ -= (shift_ == 0) & 1;
    --shift_ &= 7;
    return *this;
  }

  // Post decrement the iterator by 1.
  BitIterator operator--(int) {
    auto ret = *this;
    --(*this);
    return ret;
  }

  // Return the Iterator that has been incremented by v.
  // v must be possitive.
  BitIterator operator+(int64_t v) const {
    YACL_ENFORCE(v >= 0);
    BitIterator ret(bytes_, shift_ + v);

    return ret;
  }

  // Check if two iterators point to the same bit.
  bool operator==(const BitIterator& cmp) const {
    return bytes_ == cmp.bytes_ && shift_ == cmp.shift_;
  }

  bool operator!=(const BitIterator& cmp) const { return !(*this == cmp); }

  uint8_t* bytes_;
  uint8_t shift_;
};

template <class T>
class MatrixView {
 public:
  MatrixView() : stride_(0) {}

  MatrixView(T* data, size_t num_rows, size_t stride)
      : view_(data, num_rows * stride), stride_(stride) {}

  size_t size() const { return view_.size(); }
  size_t stride() const { return stride_; }

  size_t rows() const { return stride() ? size() / stride() : 0; }
  size_t cols() const { return stride(); }

  T& operator()(size_t idx) { return view_[idx]; }
  const T& operator()(size_t idx) const { return view_[idx]; }

  T& operator()(size_t row_idx, size_t col_idx) {
    return view_[row_idx * stride() + col_idx];
  }

  const T& operator()(size_t row_idx, size_t col_idx) const {
    return view_[row_idx * stride() + col_idx];
  }

  const absl::Span<T> operator[](size_t row_idx) const {
    YACL_ENFORCE(row_idx < rows(), "row_idx:{}, rows():{}", row_idx, rows());
    return absl::Span<T>(view_.data() + row_idx * stride(), stride());
  }

  T* data() const { return view_.data(); }

  T* data(size_t row_idx) const {
    YACL_ENFORCE(row_idx < rows());

    return view_.data() + row_idx * stride();
  }

 protected:
  absl::Span<T> view_;
  size_t stride_ = 0;
};

// a class that represents a dense binary matrix.
// The data is stored in column major format.
class DenseMtx {
 public:
  // column major, which means we call mData.row(i)
  // to get column i and vise versa.
  std::vector<uint128_t> data_;
  MatrixView<uint128_t> data_mtx_;

  // the number of rows. This will be
  // mData.cols() * 128 or a bit less.
  uint64_t bit_rows_ = 0;
  uint64_t rows_ = 0;
  uint64_t cols_ = 0;

  DenseMtx() = default;
  DenseMtx(const DenseMtx&) = default;
  DenseMtx(DenseMtx&&) = default;

  DenseMtx& operator=(const DenseMtx&) = default;
  DenseMtx& operator=(DenseMtx&&) = default;

  DenseMtx(uint64_t rows, uint64_t cols) { resize(rows, cols); }

  // resize the given matrix. If the number of rows
  // changed then the data is invalidated.
  void resize(uint64_t rows, uint64_t cols);

  // the number of rows.
  uint64_t rows() const { return bit_rows_; }

  // the number of columns.
  uint64_t cols() const { return data_mtx_.rows(); }

  // returns a refernce to the given bit.
  BitReference operator()(uint64_t row, uint64_t col) const {
    YACL_ENFORCE(row < rows());
    YACL_ENFORCE(col < cols());

    return BitReference((uint8_t*)&data_mtx_(col, 0), row);
  }

  bool operator==(const DenseMtx& m) const {
    return rows() == m.rows() && cols() == m.cols() &&
           std::memcmp(data_.data(), m.data_.data(),
                       data_.size() * sizeof(uint128_t)) == 0;
  }

  // A referenced to the given row.
  struct Row {
    uint64_t idx;
    DenseMtx& mtx;

    // swap the data of the underlying rows.
    void swap(const Row& r);

    // returns true if the row is zero.
    bool IsZero() const;

    // xor the given row into this one.
    void operator^=(const Row& r);
  };

  // returns a refernce to the given row.
  Row row(uint64_t i) const { return Row{i, (DenseMtx&)*this}; }

  // returns a refernce to the given column.
  absl::Span<uint128_t> col(uint64_t i) { return data_mtx_[i]; }

  // returns a refernce to the given column.
  absl::Span<const uint128_t> col(uint64_t i) const { return data_mtx_[i]; }

  // multiply this matrix by m and return the result.
  DenseMtx Mult(const DenseMtx& m);

  // multiply this matrix by m and return the result.
  DenseMtx operator*(const DenseMtx& m) { return Mult(m); }

  // add this matrix with m and return the result.
  DenseMtx Add(DenseMtx& m);

  // add this matrix with m and return the result.
  DenseMtx operator+(DenseMtx& m) { return Add(m); }

  // returns the identity matrix of the given size.
  static DenseMtx Identity(uint64_t n);

  // return the inverse of this matrix.
  DenseMtx Invert() const;
};

std::ostream& operator<<(std::ostream& o, const DenseMtx& H);

}  // namespace psi::rr22::okvs
