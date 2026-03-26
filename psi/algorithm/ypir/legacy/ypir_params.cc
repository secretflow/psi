// Copyright 2026 The secretflow authors.
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

#include "psi/algorithm/ypir/legacy/ypir_params.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <utility>

#include "yacl/base/exception.h"

namespace psi::ypir::ypir_internal {
namespace {

uint64_t Log2Exact(uint64_t n) {
  uint64_t r = 0;
  while (n > 1) {
    n >>= 1;
    ++r;
  }
  return r;
}

// Thread-local buffer pool for PackrlweOnline* recursive functions.
// Each recursion depth gets 6 dedicated scratch vectors:
//   0=tmp, 1=tmp1, 2=tmp2, 3=result, 4=b_e (left child result), 5=b_o (right
//   child result)
// Slots 4/5 eliminate the ~1022 × 16 KB heap copies for b_e/b_o in
// PackrlweOnlineConstantRowsImpl.
struct PackrlwePool {
  static constexpr size_t kMaxDepth = 16;
  static constexpr size_t kSlots = 6;
  size_t len = 0;
  std::array<std::array<std::vector<uint64_t>, kSlots>, kMaxDepth> v;

  void Init(size_t length) {
    if (len == length) return;
    len = length;
    for (auto& row : v)
      for (auto& buf : row) buf.resize(length);
  }

  std::vector<uint64_t>& Get(size_t depth, size_t slot) {
    return v[depth][slot];
  }
};

thread_local PackrlwePool g_packrlwe_pool;

}  // namespace

Secret::Secret(uint64_t length, uint64_t cmod) : len_(length), mod_(cmod) {
  std::mt19937_64 rng(static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  std::uniform_int_distribution<int> dist(-1, 1);
  data.reserve(length);
  for (uint64_t i = 0; i < length; ++i) {
    int tmp = dist(rng);
    if (tmp == -1) {
      data.push_back(cmod - 1);
    } else {
      data.push_back(static_cast<uint64_t>(tmp));
    }
  }
  is_ntt_ = false;
}

FheParams::FheParams(uint64_t rlwe_degree, uint64_t rlwe_ct_modulus,
                     uint64_t rlwe_pt_modulus, uint64_t lwe_dimension,
                     uint64_t lwe_ct_modulus, uint64_t lwe_pt_modulus,
                     double sigma, double sigma_ring, AutoParams auto_params,
                     DecompParams decomp_params)
    : rlwe_degree_(rlwe_degree),
      rlwe_ct_modulus_(rlwe_ct_modulus),
      rlwe_pt_modulus_(rlwe_pt_modulus),
      lwe_dimension_(lwe_dimension),
      lwe_ct_modulus_(lwe_ct_modulus),
      lwe_pt_modulus_(lwe_pt_modulus),
      sigma_(sigma),
      sigma_ring_(sigma_ring),
      auto_params_(auto_params),
      decomp_params_(decomp_params),
      ntt_(rlwe_degree, rlwe_ct_modulus, kRootOfUnityCrt) {
  YACL_ENFORCE(rlwe_degree_ > 0);
  YACL_ENFORCE(lwe_dimension_ > 0);
  YACL_ENFORCE(rlwe_ct_modulus_ > 0);
  YACL_ENFORCE(lwe_ct_modulus_ > 0);
  YACL_ENFORCE(rlwe_pt_modulus_ > 0);
  YACL_ENFORCE(lwe_pt_modulus_ > 0);
  YACL_ENFORCE(IsPowerOfTwo(rlwe_degree_), "rlwe_degree must be power of two");

  rlwe_degree_log2_ = Log2Exact(rlwe_degree_);
}

void FheParams::set_persudo_matrix_simplepir(uint64_t row) {
  SetPseudorandomMatrixSimplepir(persudo_matrix_simplepir_,
                                 persudo_matrix_simplepir_flat_, row,
                                 lwe_dimension_, lwe_ct_modulus_, prg_);
}

void FheParams::set_persudo_matrix_doublepir(uint64_t row) {
  SetPseudorandomMatrixDoublepir(persudo_matrix_doublepir_,
                                 persudo_matrix_doublepir_flat_, row,
                                 rlwe_degree_, rlwe_ct_modulus_, prg_);
}

void FheParams::set_persudo_hcube_ypir() {
  uint64_t expo = rlwe_degree_log2_;
  SetPseudorandomHypercubeYpir(persudo_hcube_ypir_, expo, auto_params_.t,
                               rlwe_degree_, rlwe_ct_modulus_, prg_);
}

void FheParams::set_automap(std::vector<uint64_t>& idx) {
  SetAutomap(automap_, rlwe_degree_, idx);
}

void FheParams::set_precomputed_pt(uint64_t max_lh) {
  SetPrecomputedPt(precomputed_pt_, rlwe_degree_, max_lh, ntt_forward_);
}

PirParams::PirParams(uint64_t rows, uint64_t cols) : rows_(rows), cols_(cols) {
  YACL_ENFORCE(rows_ > 0);
  YACL_ENFORCE(cols_ > 0);
}

// ======================================================================
// YPIR utilities that depend on FheParams.
// ======================================================================

void ApplyAutoNttForm(const std::vector<uint64_t>& vec,
                      std::vector<uint64_t>& result, uint64_t idx,
                      const FheParams& fparm) {
  const uint64_t length = vec.size();
  idx = idx % (2 * length);
  const auto& automap = fparm.get_automap(GetLog2(idx - 1) - 1);

  if (vec.data() == result.data()) {
    static thread_local std::vector<uint64_t> scratch;
    scratch.resize(length);
    std::copy(vec.begin(), vec.end(), scratch.begin());
    for (uint64_t i = 0; i < length; ++i) {
      result[i] = scratch[automap[i]];
    }
    return;
  }

  if (result.size() != length) {
    result.resize(length);
  }
  for (uint64_t i = 0; i < length; ++i) {
    result[i] = vec[automap[i]];
  }
}

void KeyswitchPreprocess(
    const std::vector<std::vector<uint64_t>>& ksk_a,
    std::vector<uint64_t>& a_in, std::vector<uint64_t>& a_out,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
    const FheParams& fparm) {
  const uint64_t z = fparm.get_z_auto();
  const uint64_t t = fparm.get_t_auto();
  const uint64_t b = fparm.get_b_auto();
  const uint64_t length = fparm.get_poly_degree();
  const uint64_t modulus = fparm.get_rlwe_cmod();

  YpirHexlNtt& ntt = fparm.get_ntt();
  std::vector<std::vector<uint64_t>> decomp_a(t,
                                              std::vector<uint64_t>(length, 0));
  ntt.Inverse(a_in.data(), length);
  for (uint64_t i = 0; i < length; ++i) {
    uint64_t val = a_in[i] >> b;
    for (uint64_t j = 0; j < t; ++j) {
      decomp_a[j][i] = val & ((1ULL << z) - 1);
      val >>= z;
    }
  }
  for (uint64_t i = 0; i < t; ++i) {
    ntt.Forward(decomp_a[i].data(), length);
  }

  decomp_buf.push_back(std::move(decomp_a));
  if (a_out.size() != length) {
    a_out.resize(length);
  }
  std::fill(a_out.begin(), a_out.end(), 0);
  std::vector<uint64_t> tmp(length, 0);
  for (uint64_t i = 0; i < t; ++i) {
    EltwiseMultMod(tmp.data(), ksk_a[i].data(), decomp_buf.back()[i].data(),
                   length, modulus);
    EltwiseSubMod(a_out.data(), a_out.data(), tmp.data(), length, modulus);
  }
}

void KeyswitchOnline(const std::vector<std::vector<uint64_t>>& ksk_b,
                     std::vector<uint64_t>& b_in, std::vector<uint64_t>& b_out,
                     std::vector<std::vector<uint64_t>>& decomp_buf,
                     const FheParams& fparm) {
  const uint64_t t = fparm.get_t_auto();
  const uint64_t length = fparm.get_poly_degree();
  const uint64_t modulus = fparm.get_rlwe_cmod();
  // Skip copy if b_in and b_out are already the same object (in-place call).
  if (b_out.data() != b_in.data()) {
    b_out = b_in;
  }
  // Reuse a thread-local buffer to avoid repeated heap allocation.
  // EltwiseMultMod fully overwrites tmp, so no zero-init needed.
  static thread_local std::vector<uint64_t> tmp;
  if (tmp.size() < length) tmp.resize(length);
  for (uint64_t i = 0; i < t; ++i) {
    EltwiseMultMod(tmp.data(), ksk_b[i].data(), decomp_buf[i].data(), length,
                   modulus);
    EltwiseSubMod(b_out.data(), b_out.data(), tmp.data(), length, modulus);
  }
}

namespace {
void EvalAutoPreprocess(
    std::vector<uint64_t>& a_in, std::vector<uint64_t>& a_out,
    const std::vector<std::vector<uint64_t>>& ksk_a, uint64_t idx,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
    const FheParams& fparm) {
  ApplyAutoNttForm(a_in, a_in, idx, fparm);
  KeyswitchPreprocess(ksk_a, a_in, a_out, decomp_buf, fparm);
}

void EvalAutoOnline(std::vector<uint64_t>& b_in, std::vector<uint64_t>& b_out,
                    const std::vector<std::vector<uint64_t>>& ksk_b,
                    uint64_t idx,
                    std::vector<std::vector<uint64_t>>& decomp_buf,
                    const FheParams& fparm) {
  // Permute b_in into b_out (out-of-place: no scratch copy needed).
  // Then key-switch b_out in place, avoiding the extra copy in KeyswitchOnline.
  ApplyAutoNttForm(b_in, b_out, idx, fparm);
  KeyswitchOnline(ksk_b, b_out, b_out, decomp_buf, fparm);
}

std::vector<uint64_t> PackrlwePreprocessImpl(
    const std::vector<std::vector<uint64_t>>& a, uint64_t start,
    uint64_t stride, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_a,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
    const FheParams& fparm) {
  if (l == 0) {
    return a[start];
  }

  const uint64_t length = fparm.get_poly_degree();
  const uint64_t modulus = fparm.get_rlwe_cmod();
  const uint64_t expo = GetLog2(length);

  std::vector<uint64_t> a_e = PackrlwePreprocessImpl(
      a, start, stride << 1, l - 1, h, ksk_a, decomp_buf, fparm);
  std::vector<uint64_t> a_o = PackrlwePreprocessImpl(
      a, start + stride, stride << 1, l - 1, h, ksk_a, decomp_buf, fparm);

  const std::vector<uint64_t>& pt = fparm.get_precomputed_pt(l + h);
  std::vector<uint64_t> tmp(length, 0);
  std::vector<uint64_t> tmp1(length, 0);
  std::vector<uint64_t> tmp2(length, 0);
  std::vector<uint64_t> result(length, 0);

  EltwiseMultMod(tmp.data(), pt.data(), a_o.data(), length, modulus);
  EltwiseAddMod(result.data(), a_e.data(), tmp.data(), length, modulus);
  EltwiseSubMod(tmp1.data(), a_e.data(), tmp.data(), length, modulus);

  EvalAutoPreprocess(tmp1, tmp2,
                     ksk_a[GetAutokeyIdx((1ULL << (l + h)) + 1, expo)],
                     (1ULL << (l + h)) + 1, decomp_buf, fparm);
  EltwiseAddMod(result.data(), result.data(), tmp2.data(), length, modulus);
  return result;
}

std::vector<uint64_t> PackrlweOnlineImpl(
    const std::vector<std::vector<uint64_t>>& b, uint64_t start,
    uint64_t stride, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm, uint64_t depth = 0) {
  if (l == 0) {
    return b[start];
  }

  const uint64_t length = fparm.get_poly_degree();
  const uint64_t modulus = fparm.get_rlwe_cmod();
  const uint64_t expo = GetLog2(length);

  std::vector<uint64_t> b_e =
      PackrlweOnlineImpl(b, start, stride << 1, l - 1, h, ksk_b, decomp_buf,
                         ptr, fparm, depth + 1);
  std::vector<uint64_t> b_o =
      PackrlweOnlineImpl(b, start + stride, stride << 1, l - 1, h, ksk_b,
                         decomp_buf, ptr, fparm, depth + 1);

  const std::vector<uint64_t>& pt = fparm.get_precomputed_pt(l + h);

  g_packrlwe_pool.Init(length);
  auto& tmp = g_packrlwe_pool.Get(depth, 0);
  auto& tmp1 = g_packrlwe_pool.Get(depth, 1);
  auto& tmp2 = g_packrlwe_pool.Get(depth, 2);
  auto& result = g_packrlwe_pool.Get(depth, 3);

  EltwiseMultMod(tmp.data(), pt.data(), b_o.data(), length, modulus);
  EltwiseAddMod(result.data(), b_e.data(), tmp.data(), length, modulus);
  EltwiseSubMod(tmp1.data(), b_e.data(), tmp.data(), length, modulus);

  EvalAutoOnline(tmp1, tmp2, ksk_b[GetAutokeyIdx((1ULL << (l + h)) + 1, expo)],
                 (1ULL << (l + h)) + 1, decomp_buf[ptr++], fparm);
  EltwiseAddMod(result.data(), result.data(), tmp2.data(), length, modulus);
  return result;
}

// Output-parameter variant: writes result directly into `output` (a pool slot
// owned by the parent frame), eliminating the ~16 KB copy-on-return that the
// previous return-by-value version produced at each of 1023 recursive calls.
void PackrlweOnlineConstantRowsImpl(
    std::vector<uint64_t>& output, const std::vector<uint64_t>& b,
    uint64_t start, uint64_t stride, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm, uint64_t depth = 0) {
  const uint64_t length = fparm.get_poly_degree();
  g_packrlwe_pool.Init(length);

  if (l == 0) {
    std::fill(output.begin(), output.end(), b[start]);
    return;
  }

  const uint64_t modulus = fparm.get_rlwe_cmod();
  const uint64_t expo = GetLog2(length);

  // Pool slots at this depth:
  //   0=tmp, 1=tmp1, 2=tmp2  (scratch)
  //   4=b_e (left child writes here), 5=b_o (right child writes here)
  // `output` is provided by the parent (or the top-level wrapper).
  auto& tmp = g_packrlwe_pool.Get(depth, 0);
  auto& tmp1 = g_packrlwe_pool.Get(depth, 1);
  auto& tmp2 = g_packrlwe_pool.Get(depth, 2);

  if (l == 1) {
    const std::vector<uint64_t>& pt = fparm.get_precomputed_pt(l + h);
    EltwiseFMAMod(tmp.data(), pt.data(), b[start + stride], nullptr, length,
                  modulus);
    const uint64_t scalar = b[start];
    for (uint64_t i = 0; i < length; ++i) {
      output[i] = scalar + tmp[i];
      if (output[i] >= modulus) output[i] -= modulus;
      tmp1[i] = scalar >= tmp[i] ? scalar - tmp[i] : scalar + modulus - tmp[i];
    }
    EvalAutoOnline(tmp1, tmp2,
                   ksk_b[GetAutokeyIdx((1ULL << (l + h)) + 1, expo)],
                   (1ULL << (l + h)) + 1, decomp_buf[ptr++], fparm);
    EltwiseAddMod(output.data(), output.data(), tmp2.data(), length, modulus);
    return;
  }

  // Recurse: children write directly into pool slots 4 and 5 at this depth,
  // avoiding any heap allocation or copy for the intermediate results.
  auto& b_e = g_packrlwe_pool.Get(depth, 4);
  auto& b_o = g_packrlwe_pool.Get(depth, 5);

  PackrlweOnlineConstantRowsImpl(b_e, b, start, stride << 1, l - 1, h, ksk_b,
                                 decomp_buf, ptr, fparm, depth + 1);
  PackrlweOnlineConstantRowsImpl(b_o, b, start + stride, stride << 1, l - 1, h,
                                 ksk_b, decomp_buf, ptr, fparm, depth + 1);

  const std::vector<uint64_t>& pt = fparm.get_precomputed_pt(l + h);

  EltwiseMultMod(tmp.data(), pt.data(), b_o.data(), length, modulus);
  EltwiseAddMod(output.data(), b_e.data(), tmp.data(), length, modulus);
  EltwiseSubMod(tmp1.data(), b_e.data(), tmp.data(), length, modulus);

  EvalAutoOnline(tmp1, tmp2, ksk_b[GetAutokeyIdx((1ULL << (l + h)) + 1, expo)],
                 (1ULL << (l + h)) + 1, decomp_buf[ptr++], fparm);
  EltwiseAddMod(output.data(), output.data(), tmp2.data(), length, modulus);
}
}  // namespace

std::vector<uint64_t> PackrlwePreprocess(
    std::vector<std::vector<uint64_t>>& a, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_a,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
    const FheParams& fparm) {
  return PackrlwePreprocessImpl(a, 0, 1, l, h, ksk_a, decomp_buf, fparm);
}

std::vector<uint64_t> PackrlweOnline(
    std::vector<std::vector<uint64_t>>& b, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm) {
  return PackrlweOnlineImpl(b, 0, 1, l, h, ksk_b, decomp_buf, ptr, fparm);
}

std::vector<uint64_t> PackrlweOnlineConstantRows(
    const std::vector<uint64_t>& b, uint64_t l, uint64_t h,
    const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
    std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf, uint64_t& ptr,
    const FheParams& fparm) {
  g_packrlwe_pool.Init(fparm.get_poly_degree());
  // Use the depth-0 result slot as output; one final copy on return.
  auto& result = g_packrlwe_pool.Get(0, 3);
  PackrlweOnlineConstantRowsImpl(result, b, 0, 1, l, h, ksk_b, decomp_buf, ptr,
                                 fparm, 0);
  return result;
}

void Cdks21Lwe2RlweInplace(uint64_t* lwe_a, uint64_t degree, uint64_t cmod,
                           YpirHexlNtt& ntt) {
  uint64_t a0 = lwe_a[0];

  for (uint64_t i = 1; i < (degree + 1) / 2; ++i) {
    uint64_t left_val = lwe_a[i];
    uint64_t right_val = lwe_a[degree - i];
    lwe_a[i] = right_val ? cmod - right_val : 0;
    lwe_a[degree - i] = left_val ? cmod - left_val : 0;
  }

  if (degree % 2 == 0) {
    uint64_t mid = degree / 2;
    uint64_t val = lwe_a[mid];
    lwe_a[mid] = val ? cmod - val : 0;
  }

  lwe_a[0] = a0;
  ntt.Forward(lwe_a, degree);
}

}  // namespace psi::ypir::ypir_internal
