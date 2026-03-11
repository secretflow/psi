#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "psi/algorithm/spiral/params.h"

namespace psi::ypir {

using Params = spiral::Params;

class Convolution {
 public:
  explicit Convolution(size_t n);

  const Params& params() const { return params_; }

  std::vector<uint32_t> ntt(const std::vector<uint32_t>& a) const;
  std::vector<uint32_t> raw(const std::vector<uint32_t>& a) const;

  std::vector<uint32_t> pointwise_mul(const std::vector<uint32_t>& a,
                                      const std::vector<uint32_t>& b) const;

  std::vector<uint32_t> convolve(const std::vector<uint32_t>& a,
                                 const std::vector<uint32_t>& b) const;

 private:
  size_t n_;
  Params params_;
};

std::vector<uint32_t> NaiveNegacyclicConvolve(const std::vector<uint32_t>& a,
                                              const std::vector<uint32_t>& b);

std::vector<uint32_t> NegacyclicMatrixU32(const std::vector<uint32_t>& b);

std::vector<uint32_t> NegacyclicPermU32(const std::vector<uint32_t>& a);

}  // namespace psi::ypir