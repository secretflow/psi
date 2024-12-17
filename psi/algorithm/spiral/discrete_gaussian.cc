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

#include "psi/algorithm/spiral/discrete_gaussian.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "yacl/crypto/tools/prg.h"

namespace psi::spiral {

DiscreteGaussian::DiscreteGaussian(double noise_width) {
  max_val_ = static_cast<int64_t>(
      std::ceil((noise_width * static_cast<double>(kNumWidths))));
  std::vector<double> table;
  table.reserve(2 * max_val_ + 1);
  double total = 0.0;
  for (int64_t i = -max_val_; i <= max_val_; ++i) {
    double p_val = std::exp(-PI * std::pow(static_cast<double>(i), 2) /
                            std::pow(noise_width, 2));
    table.push_back(p_val);
    total += p_val;
  }
  double cum_prob = 0.0;
  for (auto p : table) {
    cum_prob += p / total;
    double cum_prob_double = std::round(
        cum_prob * static_cast<double>(std::numeric_limits<uint64_t>::max()));
    auto cum_prob_u64 = static_cast<uint64_t>(cum_prob_double);
    // avoid overflow lead to UB
    if (cum_prob_double >=
        static_cast<double>(std::numeric_limits<uint64_t>::max())) {
      cum_prob_u64 = std::numeric_limits<uint64_t>::max();
    }
    cdf_table_.push_back(cum_prob_u64);
  }
}

uint64_t DiscreteGaussian::Sample(uint64_t modulus,
                                  yacl::crypto::Prg<uint64_t>& prg) const {
  uint64_t sampled_val = prg();
  size_t len = (2 * static_cast<size_t>(max_val_) + 1);
  uint64_t output = 0;

  for (int i = len - 1; i >= 0; i--) {
    int64_t out_val = static_cast<int64_t>(i) - max_val_;
    if (out_val < 0) {
      out_val += (static_cast<int64_t>(modulus));
    }
    auto out_val_u64 = static_cast<uint64_t>(out_val);
    uint64_t point = cdf_table_[i];
    if (sampled_val <= point) {
      output = out_val_u64;
    }
  }
  return output;
}
void DiscreteGaussian::SampleMatrix(const Params& params, PolyMatrixRaw& p,
                                    yacl::crypto::Prg<uint64_t>& prg) const {
  auto modulus = params.Modulus();

  for (size_t r = 0; r < p.Rows(); ++r) {
    for (size_t c = 0; c < p.Cols(); ++c) {
      auto poly = p.Poly(r, c);
      for (size_t i = 0; i < poly.size(); ++i) {
        poly[i] = this->Sample(modulus, prg);
      }
    }
  }
}

}  // namespace psi::spiral
