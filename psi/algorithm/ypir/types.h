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
#include <utility>
#include <vector>

#include "psi/algorithm/spiral/poly_matrix.h"

namespace psi::ypir {

using namespace psi::spiral;

// Precomputation tuple type for packing operations
using Precomp =
    std::vector<std::tuple<PolyMatrixNtt, std::vector<PolyMatrixNtt>,
                           std::vector<std::vector<uint64_t>>>>;

// Forward declaration for YPirServer template class
template <typename T>
class YPirServer;

// Convenience alias for the most common type
using YServer = YPirServer<uint16_t>;

// Offline precomputation values for SimplePIR and DoublePIR
struct OfflinePrecomputedValues {
  // SimplePIR fields
  std::vector<uint64_t> hint_0;  // First hint
  // DoublePIR fields
  std::vector<uint64_t> hint_1;  // Second dimension hint
  std::vector<PolyMatrixNtt>
      pseudorandom_query_1;  // Pseudorandom query for second dimension
  std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>> y_constants;
  std::shared_ptr<YServer> smaller_server;
  // Packing-related fields
  std::vector<std::vector<PolyMatrixNtt>> prepacked_lwe;
  std::vector<PolyMatrixNtt> fake_pack_pub_params;
  Precomp precomp;
};

}  // namespace psi::ypir
