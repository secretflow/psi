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

#include "psi/algorithm/spiral/util.h"

#include <cstddef>
#include <utility>

namespace psi::spiral::util {

Params GetLargerParam() {
  std::size_t poly_len{2048};
  std::vector<std::uint64_t> moduli{268369921, 249561089};
  double noise_width{6.4};

  PolyMatrixParams poly_matrix_params(2, 256, 22, 3, 5, 5, 7);
  QueryParams query_params(9, 6, 4);

  return Params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));
}

Params GetTestParam() {
  std::size_t poly_len{2048};
  std::vector<std::uint64_t> moduli{268369921, 249561089};
  double noise_width{6.4};

  PolyMatrixParams poly_matrix_params(2, 256, 20, 4, 8, 56, 8);
  QueryParams query_params(9, 6, 1);

  return Params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));
}

Params GetDefaultParam() {
  std::size_t poly_len{2048};
  std::vector<std::uint64_t> moduli{268369921, 249561089};

  double noise_width{6.4};

  PolyMatrixParams poly_matrix_params(2, 256, 21, 4, 8, 8, 6);
  QueryParams query_params(9, 6, 1);

  return Params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));
}

Params GetFastParam() {
  std::size_t poly_len{2048};
  std::vector<std::uint64_t> moduli{268369921, 249561089};

  double noise_width{6.4};

  PolyMatrixParams poly_matrix_params(2, 256, 21, 4, 8, 8, 4);
  QueryParams query_params(9, 6, 1);

  return Params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));
}

Params GetFastExpansionTestingParam() {
  std::size_t poly_len{2048};
  std::vector<std::uint64_t> moduli{268369921, 249561089};
  double noise_width{6.4};

  PolyMatrixParams poly_matrix_params(2, 256, 20, 4, 8, 8, 4);

  QueryParams query_params(6, 2, 1);

  return Params(poly_len, std::move(moduli), noise_width,
                std::move(poly_matrix_params), std::move(query_params));
}

std::size_t CalcIndex(const std::vector<std::size_t>& indices,
                      const std::vector<std::size_t>& length) {
  std::size_t idx{0};
  std::size_t prod{1};

  for (size_t i = indices.size(); i-- > 0;) {
    idx += (indices[i] * prod);
    prod *= length[i];
  }
  return idx;
}

std::vector<uint8_t> ConvertCoeffsToBytes(
    const std::vector<uint64_t>& coeff_array, size_t logt) {
  size_t len = arith::UintNum(coeff_array.size() * logt, 8);
  std::vector<uint8_t> bytes(len);

  size_t room = 8;
  size_t j = 0;
  for (const auto l : coeff_array) {
    uint64_t src = l;
    size_t rest = logt;
    while (rest != 0 && j < bytes.size()) {
      size_t shift = std::min(room, rest);
      bytes[j] = (bytes[j] << shift);
      bytes[j] = (bytes[j] | (src >> (logt - shift)));
      src = src << shift;
      room -= shift;
      rest -= shift;
      if (room == 0) {
        ++j;
        room = 8;
      }
    }
  }
  return bytes;
}

std::vector<uint8_t> ConvertCoeffsToBytes(absl::Span<uint64_t> coeff_array,
                                          size_t logt) {
  size_t len = arith::UintNum(coeff_array.size() * logt, 8);
  std::vector<uint8_t> bytes(len);

  size_t room = 8;
  size_t j = 0;
  for (const auto l : coeff_array) {
    uint64_t src = l;
    size_t rest = logt;
    while (rest != 0 && j < bytes.size()) {
      size_t shift = std::min(room, rest);
      bytes[j] = (bytes[j] << shift);
      bytes[j] = (bytes[j] | (src >> (logt - shift)));
      src = src << shift;
      room -= shift;
      rest -= shift;
      if (room == 0) {
        ++j;
        room = 8;
      }
    }
  }
  return bytes;
}

std::vector<uint64_t> ConvertBytesToCoeffs(
    size_t logt, size_t offset, size_t size,
    const std::vector<uint8_t>& byte_array) {
  size_t coeff_array_size = arith::UintNum(8 * size, logt);
  std::vector<uint64_t> coeff_array(coeff_array_size);

  size_t room = logt;
  size_t flag = 0;
  for (size_t i = 0; i < size; ++i) {
    uint32_t src = static_cast<uint32_t>(byte_array[i + offset]);

    size_t rest = 8;
    while (rest != 0) {
      if (room == 0) {
        flag += 1;
        room = logt;
      }
      size_t shift = std::min(room, rest);
      uint64_t temp = coeff_array[flag] << shift;
      coeff_array[flag] = temp | (src >> (8 - shift));
      size_t remain = (1 << (8 - shift)) - 1;

      src = (src & remain) << shift;
      room -= shift;
      rest -= shift;
    }
  }
  coeff_array[flag] = coeff_array[flag] << room;
  return coeff_array;
}

std::vector<uint8_t> ConvertBytesToU8Coeffs(
    size_t logt, size_t offset, size_t size,
    const std::vector<uint8_t>& byte_array) {
  size_t coeff_array_size = arith::UintNum(8 * size, logt);
  std::vector<uint8_t> coeff_array(coeff_array_size);

  size_t room = logt;
  size_t flag = 0;
  for (size_t i = 0; i < size; ++i) {
    uint32_t src = static_cast<uint32_t>(byte_array[i + offset]);

    size_t rest = 8;
    while (rest != 0) {
      if (room == 0) {
        flag += 1;
        room = logt;
      }
      size_t shift = std::min(room, rest);
      uint64_t temp = coeff_array[flag] << shift;
      coeff_array[flag] = temp | (src >> (8 - shift));

      size_t remain = (1 << (8 - shift)) - 1;

      src = (src & remain) << shift;
      room -= shift;
      rest -= shift;
    }
  }

  coeff_array[flag] = coeff_array[flag] << room;
  return coeff_array;
}

uint64_t ReadArbitraryBits(const std::vector<uint8_t>& buffer,
                           size_t bit_offset, size_t num_bits) {
  assert(num_bits > 0 && num_bits <= 64 && "num_bits must be between 1 and 64");

  const size_t word_offset = bit_offset / 64;
  const size_t bit_offset_within_word = bit_offset % 64;
  const size_t byte_offset = word_offset * 8;

  if (bit_offset_within_word + num_bits <= 64) {
    assert(byte_offset + 8 <= buffer.size() && "Buffer read-out-of-bounds");

    uint64_t val;
    memcpy(&val, buffer.data() + byte_offset, sizeof(uint64_t));
    const uint64_t mask = (num_bits == 64) ? ~0ULL : (1ULL << num_bits) - 1;
    return (val >> bit_offset_within_word) & mask;

  } else {
    assert(byte_offset + 16 <= buffer.size() && "Buffer read-out-of-bounds");

    uint128_t val;
    memcpy(&val, buffer.data() + byte_offset, sizeof(uint128_t));

    const uint128_t mask = (static_cast<uint128_t>(1) << num_bits) - 1;

    return static_cast<uint64_t>((val >> bit_offset_within_word) & mask);
  }
}

void WriteArbitraryBits(std::vector<uint8_t>& buffer, uint64_t val,
                        size_t bit_offset, size_t num_bits) {
  assert(num_bits > 0 && num_bits <= 64 && "num_bits must be between 1 and 64");

  const size_t word_offset = bit_offset / 64;
  const size_t bit_offset_within_word = bit_offset % 64;
  const size_t byte_offset = word_offset * 8;

  const uint64_t val_mask = (num_bits == 64) ? ~0ULL : (1ULL << num_bits) - 1;
  val &= val_mask;

  if (bit_offset_within_word + num_bits <= 64) {
    assert(byte_offset + 8 <= buffer.size() && "Buffer write-out-of-bounds");

    uint64_t current_val;
    std::memcpy(&current_val, buffer.data() + byte_offset, sizeof(uint64_t));
    const uint64_t write_mask = val_mask << bit_offset_within_word;
    current_val &= ~write_mask;
    current_val |= (val << bit_offset_within_word);
    std::memcpy(buffer.data() + byte_offset, &current_val, sizeof(uint64_t));

  } else {
    assert(byte_offset + 16 <= buffer.size() && "Buffer write-out-of-bounds");

    uint128_t current_val;
    std::memcpy(&current_val, buffer.data() + byte_offset, sizeof(uint128_t));

    const uint128_t wide_val_mask = (static_cast<uint128_t>(1) << num_bits) - 1;
    const uint128_t write_mask = wide_val_mask << bit_offset_within_word;
    current_val &= ~write_mask;
    current_val |= (static_cast<uint128_t>(val) << bit_offset_within_word);
    std::memcpy(buffer.data() + byte_offset, &current_val, sizeof(uint128_t));
  }
}
}  // namespace psi::spiral::util
