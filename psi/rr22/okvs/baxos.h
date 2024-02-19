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

#include <array>
#include <memory>
#include <ostream>

#include "absl/types/span.h"

#include "psi/rr22/okvs/dense_mtx.h"
#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/okvs/paxos.h"
#include "psi/rr22/okvs/paxos_utils.h"

namespace psi::rr22::okvs {

// a binned version of paxos. Internally calls paxos.
class Baxos {
 public:
  size_t num_items_ = 0;
  size_t num_bins_ = 0;
  size_t items_per_bin_ = 0;
  size_t weight_ = 0;
  size_t ssp_ = 0;

  // the parameters used on a single bim.
  PaxosParam paxos_param_;
  uint128_t seed_;

  bool debug_ = false;

  // when decoding, add the decoded value to the
  // output, as opposed to overwriting.
  bool add_to_decode_ = false;

  // initialize the paxos with the given parameter.
  void Init(uint64_t num_items, uint64_t bin_size, uint64_t weight,
            uint64_t ssp, PaxosParam::DenseType dt, uint128_t seed) {
    num_items_ = num_items;
    weight_ = weight;
    num_bins_ = (num_items + bin_size - 1) / bin_size;
    items_per_bin_ =
        GetBinSize(num_bins_, num_items_, ssp + std::log2(num_bins_));
    ssp_ = ssp;
    seed_ = seed;
    paxos_param_.Init(items_per_bin_, weight, ssp, dt);
  }

  // solve the system for the given input vectors.
  // inputs are the keys
  // values are the desired values that inputs should decode to.
  // output is the paxos.
  // prng should be non-null if randomized paxos is desired.
  void Solve(absl::Span<const uint128_t> inputs,
             const absl::Span<uint128_t> values, absl::Span<uint128_t> output,
             const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng = nullptr,
             uint64_t num_threads = 0);

  // solve/encode the system.
  void Solve(absl::Span<const uint128_t> inputs, const PxVector& values,
             PxVector& output,
             const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
             uint64_t num_threads, PxVector::Helper& h);

  // decode a single input given the paxos p.
  template <typename ValueType>
  ValueType Decode(const uint128_t& input, absl::Span<const ValueType> p) {
    ValueType r;
    decode(absl::Span<const uint128_t>(&input, 1), absl::Span<ValueType>(&r, 1),
           p);
    return r;
  }

  // decode the given input vector and write the result to values.
  // inputs are the keys.
  // values are the output.
  // p is the paxos vector.
  void Decode(absl::Span<const uint128_t> input, absl::Span<uint128_t> values,
              const absl::Span<uint128_t> p, uint64_t num_threads = 0);

  void Decode(absl::Span<const uint128_t> inputs, PxVector& values,
              const PxVector& p, PxVector::Helper& h, uint64_t num_threads);

  //////////////////////////////////////////
  // private impl
  //////////////////////////////////////////

  // solve/encode the system.
  template <typename IdxType>
  void ImplParSolve(absl::Span<const uint128_t> inputs, const PxVector& values,
                    PxVector& output,
                    const std::shared_ptr<yacl::crypto::Prg<uint8_t>>& prng,
                    uint64_t num_threads, PxVector::Helper& h);

  // create the desired number of threads and split up the work.
  template <typename IdxType>
  void ImplParDecode(absl::Span<const uint128_t> inputs, PxVector& values,
                     const PxVector& p, PxVector::Helper& h,
                     uint64_t num_threads);

  // decode the given inputs based on the paxos p. The output is written to
  // values.
  template <typename IdxType>
  void ImplDecodeBatch(absl::Span<const uint128_t> inputs, PxVector& values,
                       const PxVector& p, PxVector::Helper& h);

  // decode the given inputs based on the paxos p. The output is written to
  // values. this differs from implDecode in that all inputs must be for the
  // same paxos bin.
  template <typename IdxType>
  void ImplDecodeBin(uint64_t bin_idx, absl::Span<uint128_t> hashes,
                     PxVector& values, PxVector& valuesBuff,
                     absl::Span<uint64_t> inIdxs, const PxVector& p,
                     PxVector::Helper& h, Paxos<IdxType>& paxos);

  // the size of the paxos.
  uint64_t size() {
    return uint64_t(num_bins_ *
                    (paxos_param_.sparse_size + paxos_param_.dense_size));
  }

  static uint64_t GetBinSize(uint64_t num_bins, uint64_t num_items,
                             uint64_t ssp);

  uint64_t BinIdxCompress(const uint128_t& h) {
    auto h64 = Galois128(h).get<uint64_t>();
    auto h32 = Galois128(h).get<uint32_t>();

    return h64[0] ^ h64[1] ^ h32[3];
  }

  uint64_t ModNumBins(const uint128_t& h) {
    return BinIdxCompress(h) % num_bins_;
  }

  void Check(absl::Span<uint128_t> inputs, PxVector values, PxVector output) {
    auto h = values.DefaultHelper();
    auto v2 = h.NewVec(values.size());
    Decode(inputs, v2, output, h, 1);

    for (uint64_t i = 0; i < values.size(); ++i) {
      YACL_ENFORCE(
          h.eq(v2[i], values[i]),
          "paxos failed to encode. inputs[{}]: {} seed:{}  n: {} m: {} ", i,
          inputs[i], seed_, size(), inputs.size());
    }
  }
};

}  // namespace psi::rr22::okvs
