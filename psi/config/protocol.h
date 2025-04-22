// Copyright 2025 Ant Group Co., Ltd.
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

#include "psi/config/curve.h"

namespace psi::api {

// PSI protocols.
enum class PsiProtocol : std::uint8_t {
  PROTOCOL_UNSPECIFIED = 0,

  // Semi-Honest Secure

  // [Mea86]C. Meadows, "A More Efficient Cryptographic Matchmaking Protocol
  // for Use in the Absence of a Continuously Available Third Party," 1986 IEEE
  // Symposium on Security and Privacy, Oakland, CA, USA, 1986, pp. 134-134,
  // doi: 10.1109/SP.1986.10022.
  PROTOCOL_ECDH = 1,

  // Efficient Batched Oblivious PRF with Applications to Private Set
  // Intersection https://eprint.iacr.org/2016/799.pdf
  PROTOCOL_KKRT = 2,

  // Blazing Fast PSI https://eprint.iacr.org/2022/320.pdf
  PROTOCOL_RR22 = 3,

  // Multi-party PSI based on ECDH (Say A, B, C (receiver))
  // Notice: two-party intersection cardinarlity leak (|A intersect B|)
  PROTOCOL_ECDH_3PC = 4,

  // Iterative running 2-party ecdh psi to get n-party PSI.
  // Notice: two-party intersection leak
  PROTOCOL_ECDH_NPC = 5,

  // Iterative running 2-party kkrt psi to get n-party PSI.
  // Notice: two-party intersection leak
  PROTOCOL_KKRT_NPC = 6,

  // Differentially-Private PSI https://arxiv.org/pdf/2208.13249.pdf
  // bases on ECDH-PSI, and provides: Differentially private PSI results.
  PROTOCOL_DP = 7,
};

struct EcdhParams {
  EllipticCurveType curve = EllipticCurveType::CURVE_INVALID_TYPE;

  // If not set, use default value: 4096.
  uint32_t batch_size = 4096;
};

struct Rr22Rarams {
  bool low_comm_mode = false;
};

// The input parameters of dp-psi.
struct DpParams {
  // bob sub-sampling bernoulli_distribution probability.
  double bob_sub_sampling = 0.9;
  // dp epsilon
  double epsilon = 3;
};

struct PsiProtocolConfig {
  PsiProtocol protocol = PsiProtocol::PROTOCOL_UNSPECIFIED;

  uint32_t receiver_rank = 0;

  // Since the total input may not fit in memory, the input may be splitted into
  // buckets. bucket_size indicate the number of items in each bucket.
  // If the memory of host is limited, you should set a smaller bucket size.
  // Otherwise, you should use a larger one.
  // If not set, use default value: 1 << 20.
  uint64_t bucket_size = 1 << 20;

  // Reveal result to others.
  bool broadcast_result = false;

  // For ECDH protocol extend params.
  EcdhParams ecdh_params;

  // For RR22 protocol extend params.
  Rr22Rarams rr22_params;

  // For DP protocol extend params.
  DpParams dp_params;
};

}  // namespace psi::api
