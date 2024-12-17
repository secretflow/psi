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

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "spdlog/spdlog.h"

namespace psi::spiral {

template <typename T>
void PrintVector(std::vector<T>& data) {
  std::ostringstream ss;
  ss << "[";
  for (size_t i = 0; i < data.size(); ++i) {
    ss << data[i];
    ss << (i + 1 == data.size() ? "" : ", ");
  }
  ss << "]\n";

  SPDLOG_INFO("{}", ss.str());
}

template <typename T>
void PrintVector(absl::Span<T> data) {
  std::ostringstream ss;
  ss << "[";
  for (size_t i = 0; i < data.size(); ++i) {
    ss << data[i];
    ss << (i + 1 == data.size() ? "" : ", ");
  }
  ss << "]\n";

  SPDLOG_INFO("{}", ss.str());
}

template <typename T>
void PrintVector(std::string_view name, std::vector<T>& data) {
  std::ostringstream ss;

  ss << name << ": \n"
     << "[";
  for (size_t i = 0; i < data.size(); ++i) {
    ss << data[i];
    ss << (i + 1 == data.size() ? "" : ", ");
  }
  ss << "]\n";
  SPDLOG_INFO("{}", ss.str());
}

template <typename T>
void PrintVector(std::string_view name, absl::Span<T> data) {
  std::ostringstream ss;

  ss << name << ": \n"
     << "[";
  for (size_t i = 0; i < data.size(); ++i) {
    ss << data[i];
    ss << (i + 1 == data.size() ? "" : ", ");
  }
  ss << "]\n";
  SPDLOG_INFO("{}", ss.str());
}

constexpr std::size_t kMaxModuli{4};

constexpr std::size_t kMinQ2Bits{14};

constexpr std::size_t kHammingWeight{256};

constexpr std::size_t kSeedLength{32};

constexpr std::uint64_t kPackedOffset2{32};

// the range of v1 and v2
// ref: in Spiral paper Page-26: we consider all database configurations v1 v2
// \in [2, 11]
constexpr std::size_t kMaxDbDim{11};

// map q2Bits to q2 value
constexpr std::array<uint64_t, 37> kQ2Values{0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             0,
                                             12289,
                                             12289,
                                             61441,
                                             65537,
                                             65537,
                                             520193,
                                             786433,
                                             786433,
                                             3604481,
                                             7340033,
                                             16515073,
                                             33292289,
                                             67043329,
                                             132120577,
                                             268369921,
                                             469762049,
                                             1073479681,
                                             2013265921,
                                             4293918721ULL,
                                             8588886017ULL,
                                             17175674881ULL,
                                             34359214081ULL,
                                             68718428161ULL};

}  // namespace psi::spiral