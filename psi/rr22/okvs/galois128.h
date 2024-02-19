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

#include <ostream>
#include <variant>

#include "absl/strings/escaping.h"
#include "spdlog/spdlog.h"
#include "yacl/base/block.h"
#include "yacl/base/exception.h"
#include "yacl/base/int128.h"

// Galois field 2^128
// polynoimal : x^127+x^7+x^2+x+1

namespace psi::rr22::okvs {

using Galois128Type = std::variant<yacl::block, uint128_t>;

class Galois128 {
 public:
  Galois128(uint64_t a, uint64_t b);
  explicit Galois128(uint64_t v) : Galois128(0, v) {}

  Galois128(const Galois128& v) { value_ = v.value_; }

#ifdef __x86_64__
  explicit Galois128(const yacl::block& b) { value_ = b; }
#endif

  explicit Galois128(const uint128_t b);

  Galois128& operator=(const Galois128&) = default;

  Galois128 Mul(const Galois128& rhs) const;
  Galois128 Pow(std::uint64_t i) const;
  Galois128 Inv() const;

  inline Galois128 operator*(const Galois128& rhs) const {
    return Galois128(Mul(rhs));
  }

  inline Galois128 operator*(const uint128_t& rhs) const {
    return Galois128(Mul(Galois128(rhs)));
  }

  inline Galois128 operator*(const uint64_t& rhs) const {
    return Galois128(Mul(Galois128(rhs)));
  }

  const uint8_t* data() { return (const uint8_t*)&value_; }

  template <typename T>
  typename std::enable_if<std::is_standard_layout<T>::value &&
                              std::is_trivial<T>::value && (sizeof(T) <= 16) &&
                              (16 % sizeof(T) == 0),
                          std::array<T, 16 / sizeof(T)> >::type
  get() {
    std::array<T, 16 / sizeof(T)> output;
    std::memcpy(output.data(), data(), 16);
    return output;
  }

  template <typename T>
  typename std::enable_if<std::is_standard_layout<T>::value &&
                              std::is_trivial<T>::value && (sizeof(T) <= 16) &&
                              (16 % sizeof(T) == 0),
                          T>::type
  get(size_t index) {
    YACL_ENFORCE(index < 16 / sizeof(T));

    T output;
    std::memcpy(&output, data() + sizeof(T) * index, sizeof(T));
    return output;
  }

 private:
  Galois128Type value_;
};

uint128_t cc_gf128Mul(const uint128_t a, const uint128_t b);

}  // namespace psi::rr22::okvs

namespace std {

std::ostream& operator<<(std::ostream& os, psi::rr22::okvs::Galois128 x);

}
