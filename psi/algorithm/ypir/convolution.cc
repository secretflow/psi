#include "psi/algorithm/ypir/convolution.h"

#include <cassert>
#include <iostream>

#include "psi/algorithm/spiral/arith/arith.h"
#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"

namespace psi::ypir {
using namespace psi::spiral;

psi::spiral::Params CreateParams(size_t n) {
  std::size_t poly_len = n;

  std::vector<std::uint64_t> moduli{268369921, 249561089};
  double noise_width = 6.4;

  psi::spiral::PolyMatrixParams poly_matrix_params(1, 2, 28, 0, 0, 0, 0);
  psi::spiral::QueryParams query_params(1, 1, 1, true);

  return psi::spiral::Params(poly_len, std::move(moduli), noise_width,
                             std::move(poly_matrix_params),
                             std::move(query_params));
}

Convolution::Convolution(size_t n) : n_(n) { params_ = CreateParams(n); }

std::vector<uint32_t> Convolution::ntt(const std::vector<uint32_t>& a) const {
  assert(a.size() == n_);

  auto inp = PolyMatrixRaw::Zero(params_.PolyLen(), 1, 1);
  for (size_t i = 0; i < n_; ++i) {
    inp.Data()[i] = static_cast<uint64_t>(a[i]);
  }

  auto ntt_res = ToNtt(params_, inp);

  size_t out_size = params_.CrtCount() * n_;
  std::vector<uint32_t> out(out_size);
  for (size_t i = 0; i < out_size; ++i) {
    out[i] = static_cast<uint32_t>(ntt_res.Data()[i]);
  }
  return out;
}

std::vector<uint32_t> Convolution::raw(const std::vector<uint32_t>& a) const {
  assert(a.size() == params_.CrtCount() * n_);

  auto inp = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 1, 1);
  for (size_t i = 0; i < params_.CrtCount() * n_; ++i) {
    inp.Data()[i] = static_cast<uint64_t>(a[i]);
  }

  auto raw_res = FromNtt(params_, inp);

  std::vector<uint32_t> out(n_);
  int64_t modulus_i64 = static_cast<int64_t>(params_.Modulus());
  int64_t two_pow_32 = 1LL << 32;

  for (size_t i = 0; i < n_; ++i) {
    int64_t val = static_cast<int64_t>(raw_res.Data()[i]);

    assert(val < modulus_i64);
    if (val > modulus_i64 / 2) {
      val -= modulus_i64;
    }
    if (val < 0) {
      val += (modulus_i64 / two_pow_32) * two_pow_32;
      val += two_pow_32;
    }

    assert(val >= 0);
    out[i] = static_cast<uint32_t>(val % two_pow_32);
  }
  return out;
}

std::vector<uint32_t> Convolution::pointwise_mul(
    const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) const {
  size_t total_len = params_.CrtCount() * n_;
  assert(a.size() == total_len);
  assert(b.size() == total_len);

  std::vector<uint32_t> out(total_len);
  for (size_t m = 0; m < params_.CrtCount(); ++m) {
    for (size_t i = 0; i < n_; ++i) {
      size_t idx = m * n_ + i;
      uint64_t val =
          static_cast<uint64_t>(a[idx]) * static_cast<uint64_t>(b[idx]);
      out[idx] = static_cast<uint32_t>(arith::BarrettCoeffU64(params_, val, m));
    }
  }
  return out;
}

std::vector<uint32_t> Convolution::convolve(
    const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) const {
  auto a_ntt = ntt(a);
  auto b_ntt = ntt(b);
  auto res = pointwise_mul(a_ntt, b_ntt);
  return raw(res);
}

std::vector<uint32_t> NaiveNegacyclicConvolve(const std::vector<uint32_t>& a,
                                              const std::vector<uint32_t>& b) {
  assert(a.size() == b.size());
  size_t n = a.size();
  std::vector<uint32_t> res(n, 0);

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      size_t idx = (n + i - j) % n;
      uint32_t b_val = b[idx];
      if (i < j) {
        b_val = 0 - b_val;
      }
      res[i] += a[j] * b_val;
    }
  }
  return res;
}

std::vector<uint32_t> NegacyclicMatrixU32(absl::Span<const uint32_t> b) {
  size_t n = b.size();
  std::vector<uint32_t> res(n * n, 0);

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j) {
      uint32_t b_val = b[(n + i - j) % n];
      if (i < j) {
        b_val = -b_val;
      }
      res[j * n + i] = b_val;
    }
  }

  return res;
}

std::vector<uint32_t> NegacyclicPermU32(const std::vector<uint32_t>& a) {
  size_t n = a.size();
  std::vector<uint32_t> res(n);

  if (n > 0) {
    res[0] = a[0];
    for (size_t i = 1; i < n; ++i) {
      uint32_t val = a[n - i];
      res[i] = 0 - val;
    }
  }
  return res;
}

}  // namespace psi::ypir