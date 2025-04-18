// Copyright 2025
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

#include "heu/library/algorithms/elgamal/elgamal.h"
#include "yacl/math/mpint/mp_int.h"

#include "psi/wrapper/apsi/utils/common.h"
#include "psi/wrapper/apsi/yacl_channel.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {

// The maximum number of retries for receiving requests or responses
constexpr uint32_t kRecvRetryTimes = 10000;

// The interval between retries for receiving requests or responses
constexpr uint32_t kRecvRetryIntervalMs = 50;

// Compute the value of the linear function p(x)=ax+b
yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly, uint64_t data);

// Compute the value of the function p(x,y)=ax+by, this function can prevent
// a*x or b*y from exceeding the range of uint64_t
yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly, uint64_t data1,
                              uint64_t data2);

// Save the linear function and the private key of Elgamal encryption
void Save(const std::vector<uint64_t> &poly,
          const heu::lib::algorithms::elgamal::SecretKey &secret_key,
          std::ostream &out);

// Load the linear function and the private key of Elgamal encryption
void Load(std::vector<uint64_t> &poly, yacl::math::MPInt &x, std::istream &in);

// Receiver can match the key with the label using the shuffle seed and shuffle
// counter, if skip_count_check is false.
void WriteIntersectionResults(
    const std::vector<std::string> &orig_items,
    const std::vector<::apsi::Item> &items,
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    uint128_t shuffle_seed, uint64_t shuffle_counter,
    const std::string &out_file, bool skip_count_check = false,
    bool append_to_outfile = false);

void PrintTransmittedData(::apsi::network::Channel &channel);

// Obtain the string used to generate the curve according to the curve type
std::string FetchCurveName(CurveType curve_type);

void RemoveTempFile(const std::string &tmp_file);
}  // namespace psi::dkpir