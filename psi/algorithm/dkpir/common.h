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

#include "yacl/math/mpint/mp_int.h"

#include "psi/algorithm/dkpir/phe/phe.h"
#include "psi/wrapper/apsi/utils/common.h"
#include "psi/wrapper/apsi/yacl_channel.h"

namespace psi::dkpir {
// Compute the value of the linear function p(x)=ax+b
yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly,
                              const uint64_t &data);

// Compute the value of the function p(x,y)=ax+by, this function can prevent
// n*b from exceeding the range of uint32_t
yacl::math::MPInt ComputePoly(const std::vector<uint64_t> &poly,
                              const uint64_t &data1, const uint64_t &data2);

// Save the linear function and the private key of phe
void Save(const std::vector<uint64_t> &poly,
          const psi::dkpir::phe::SecretKey &secret_key, std::ostream &out);

// Load the linear function and the private key of phe
void Load(std::vector<uint64_t> &poly, yacl::math::MPInt &x, std::istream &in);

// Receiver can match the key with the label using the shuffle seed
void PrintIntersectionResults(
    const std::vector<std::string> &orig_items,
    const std::vector<::apsi::Item> &items,
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    const uint64_t &shuffle_seed, const std::string &out_file,
    bool append_to_outfile = false);

void PrintTransmittedData(::apsi::network::Channel &channel);

}  // namespace psi::dkpir