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

#include <cstdint>
#include <vector>

#include "psi/algorithm/spiral/params.h"

namespace psi::spiral::util {

Params GetFastExpansionTestingParam();

Params GetTestParam();

// this params has the best performance, but has smallest noise budget
Params GetPerformanceImproveParam();

Params GetDefaultParam();

Params GetLargerParam();

// calc multi-dimension position in 1-deimension's position
std::size_t CalcIndex(const std::vector<std::size_t>& indices,
                      const std::vector<std::size_t>& length);

std::vector<uint64_t> ConvertBytesToCoeffs(
    size_t logt, size_t offset, size_t size,
    const std::vector<uint8_t>& byte_array);

std::vector<uint8_t> ConvertBytesToU8Coeffs(
    size_t logt, size_t offset, size_t size,
    const std::vector<uint8_t>& byte_array);

std::vector<uint8_t> ConvertCoeffsToBytes(
    const std::vector<uint64_t>& coeff_array, size_t logt);

std::vector<uint8_t> ConvertCoeffsToBytes(absl::Span<uint64_t> coeff_array,
                                          size_t logt);
uint64_t ReadArbitraryBits(const std::vector<uint8_t>& buffer,
                           size_t bit_offset, size_t num_bits);
void WriteArbitraryBits(std::vector<uint8_t>& buffer, uint64_t val,
                        size_t bit_offset, size_t num_bits);
}  // namespace psi::spiral::util
