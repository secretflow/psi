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

#include "psi/algorithm/spiral/gadget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"

namespace psi::spiral::util {

size_t GetBitsPer(const Params& params, size_t dim) {
  auto modulus_log2 = params.ModulusLog2();
  if (dim == modulus_log2) {
    return 1;
  }
  return (modulus_log2 / dim) + 1;
}

PolyMatrixRaw BuildGadget(const Params& params, size_t rows, size_t cols) {
  auto g = PolyMatrixRaw::Zero(params.PolyLen(), rows, cols);

  auto nx = g.Rows();
  auto m = g.Cols();
  WEAK_ENFORCE(m % nx == 0);

  size_t num_elements = m / nx;
  size_t bits_per = GetBitsPer(params, num_elements);
  for (size_t i = 0; i < nx; ++i) {
    for (size_t j = 0; j < num_elements; ++j) {
      if (bits_per * j >= 64) {
        continue;
      }
      auto poly_idx = g.PolyStartIndex(i, i + j * nx);
      g.Data()[poly_idx] = 1ULL << (bits_per * j);
    }
  }
  return g;
}

void GadgetInvertRdim(const Params& params, PolyMatrixRaw& out,
                      const PolyMatrixRaw& in, size_t rdim) {
  WEAK_ENFORCE(out.Cols() == in.Cols());

  size_t mx = out.Rows();
  size_t num_elements = mx / rdim;
  size_t bits_per = GetBitsPer(params, num_elements);

  uint64_t mask = (static_cast<uint64_t>(1) << bits_per) - 1;
  for (size_t i = 0; i < in.Cols(); ++i) {
    for (size_t j = 0; j < rdim; ++j) {
      auto poly = in.Poly(i, j);
      for (size_t z = 0; z < params.PolyLen(); ++z) {
        uint64_t val = poly[z];
        for (size_t k = 0; k < num_elements; ++k) {
          uint64_t bit_offs = std::min(static_cast<uint64_t>(k * bits_per),
                                       static_cast<uint64_t>(64ULL));
          uint64_t piece;
          if (bit_offs >= 64) {
            piece = 0;
          } else {
            piece = (val >> bit_offs) & mask;
          }
          // assign to out
          out.Poly(j + k * rdim, i)[z] = piece;
        }
      }
    }
  }
}

}  // namespace psi::spiral::util
