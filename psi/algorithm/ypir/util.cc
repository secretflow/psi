#include "psi/algorithm/ypir/util.h"

#include <bit>
#include <cstring>
#include <vector>

#include "../spiral/arith/arith_params.h"
#include "../spiral/arith/ntt.h"
#include "../spiral/discrete_gaussian.h"
#include "../spiral/gadget.h"
#include "../spiral/poly_matrix_utils.h"
#include "yacl/base/int128.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::ypir {

using namespace psi::spiral;

std::vector<uint64_t> ConcatHorizontal(
    const std::vector<std::vector<uint64_t>>& v_a, size_t a_rows,
    size_t a_cols) {
  if (v_a.empty()) {
    return {};
  }

  size_t num_vecs = v_a.size();

  std::vector<uint64_t> out(a_rows * a_cols * num_vecs);
  size_t out_stride = num_vecs * a_cols;

  for (size_t i = 0; i < a_rows; ++i) {
    for (size_t k = 0; k < num_vecs; ++k) {
      auto src_begin = v_a[k].begin() + (i * a_cols);
      auto src_end = src_begin + a_cols;
      size_t dest_offset = (i * out_stride) + (k * a_cols);

      std::copy(src_begin, src_end, out.begin() + dest_offset);
    }
  }

  return out;
}

extern "C" {
void matMulVecPacked(uint32_t* out, const uint32_t* a, const uint32_t* b,
                     size_t aRows, size_t aCols);

void matMulVecPacked2(uint32_t* out, const uint32_t* a, const uint32_t* b_full,
                      size_t aRows, size_t aCols);

void matMulVecPacked4(uint32_t* out, const uint32_t* a, const uint32_t* b_full,
                      size_t aRows, size_t aCols);

void matMulVecPacked8(uint32_t* out, const uint32_t* a, const uint32_t* b_full,
                      size_t aRows, size_t aCols);
}

// Wrapper function that matches the Rust signature
void MatMulVecPacked(uint32_t* out, const uint32_t* a, const uint32_t* b,
                     size_t a_rows, size_t a_cols, size_t b_rows,
                     size_t b_cols) {
  // Debug output (equivalent to Rust's debug! macro)

  // Assertions
  assert(a_rows * a_cols == a_rows * a_cols);  // a.len() == a_rows * a_cols
  assert(b_rows * b_cols == b_rows * b_cols);  // b.len() == b_rows * b_cols
  assert(a_cols * 4 == b_rows);
  // Note: out.len() >= a_rows + 8 should be checked by caller

  // Dispatch based on b_cols
  if (b_cols == 1) {
    matMulVecPacked(out, a, b, a_rows, a_cols);
  } else if (b_cols == 2) {
    matMulVecPacked2(out, a, b, a_rows, a_cols);
  } else if (b_cols == 4) {
    matMulVecPacked4(out, a, b, a_rows, a_cols);
  } else if (b_cols == 8) {
    matMulVecPacked8(out, a, b, a_rows, a_cols);
  } else {
    fprintf(stderr, "Error: b_cols must be 1, 2, 4, or 8, got %zu\n", b_cols);
    assert(false && "b_cols must be 1, 2, 4, or 8");
  }
}

PolyMatrixNtt HomomorphicAutomorph(const Params& params, size_t t, size_t t_exp,
                                   const PolyMatrixNtt& ct,
                                   const PolyMatrixNtt& pub_param) {
  YACL_ENFORCE(ct.Rows() == static_cast<size_t>(2));
  YACL_ENFORCE(ct.Cols() == static_cast<size_t>(1));

  auto ct_raw = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
  FromNtt(params, ct_raw, ct);
  auto ct_auto = Automorphism(params, ct_raw, t);

  auto ginv_ct = PolyMatrixRaw::Zero(params.PolyLen(), t_exp, 1);
  psi::spiral::util::GadgetInvertRdim(params, ginv_ct, ct_auto, 1);

  auto ginv_ct_ntt =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), t_exp, 1);
  for (size_t i = 1; i < t_exp; ++i) {
    auto pol_src = ginv_ct.Poly(i, 0);
    auto pol_dst = ginv_ct_ntt.Poly(i, 0);
    ReduceCopy(params, pol_dst, pol_src);
    arith::NttForward(params, pol_dst);
  }

  auto w_times_ginv_ct = Multiply(params, pub_param, ginv_ct_ntt);

  auto ct_auto_1 = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);

  std::memcpy(ct_auto_1.Data().data(),
              ct_auto.Data().data() + ct_auto.PolyStartIndex(1, 0),
              sizeof(uint64_t) * ct_auto.NumWords());
  auto ct_auto_1_ntt = ToNtt(params, ct_auto_1);

  auto res = Add(params, ct_auto_1_ntt.PadTop(1), w_times_ginv_ct);
  return res;
}

PolyMatrixNtt RingPackLwesInner(
    const Params& params, size_t ell, size_t start_idx,
    const std::vector<PolyMatrixNtt>& rlwe_cts,
    const std::vector<PolyMatrixNtt>& pub_params,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants) {
  YACL_ENFORCE_EQ(pub_params.size(), params.PolyLenLog2());

  if (ell == 0) {
    return rlwe_cts[start_idx];
  }

  size_t step = 1ULL << (params.PolyLenLog2() - ell);
  size_t even = start_idx;
  size_t odd = start_idx + step;

  auto ct_even = RingPackLwesInner(params, ell - 1, even, rlwe_cts, pub_params,
                                   y_constants);
  auto ct_odd = RingPackLwesInner(params, ell - 1, odd, rlwe_cts, pub_params,
                                  y_constants);

  const auto& y = y_constants.first[ell - 1];
  const auto& neg_y = y_constants.second[ell - 1];

  auto y_times_ct_odd = ScalarMultiply(params, y, ct_odd);
  auto neg_y_times_ct_odd = ScalarMultiply(params, neg_y, ct_odd);

  auto ct_sum_1 = ct_even;
  AddInto(params, ct_sum_1, neg_y_times_ct_odd);
  AddInto(params, ct_even, y_times_ct_odd);

  size_t t = (1ULL << ell) + 1;
  const auto& pub_param = pub_params[params.PolyLenLog2() - 1 - (ell - 1)];
  auto ct_sum_1_automorphed =
      HomomorphicAutomorph(params, t, params.TExpLeft(), ct_sum_1, pub_param);

  return Add(params, ct_even, ct_sum_1_automorphed);
}

std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>> GenYConstants(
    const Params& params) {
  std::vector<PolyMatrixNtt> y_constants;
  std::vector<PolyMatrixNtt> neg_y_constants;

  for (size_t num_cts_log2 = 1; num_cts_log2 <= params.PolyLenLog2();
       ++num_cts_log2) {
    size_t num_cts = 1ULL << num_cts_log2;

    auto y_raw = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
    size_t idx = params.PolyLen() / num_cts;
    y_raw.Data()[idx] = 1ULL;
    auto y = ToNtt(params, y_raw);

    auto neg_y_raw = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
    neg_y_raw.Data()[idx] = params.Modulus() - 1;
    auto neg_y = ToNtt(params, neg_y_raw);

    y_constants.push_back(y);
    neg_y_constants.push_back(neg_y);
  }

  return std::make_pair(y_constants, neg_y_constants);
}

PolyMatrixNtt RingPackLwes(
    const Params& params, const std::vector<uint64_t>& b_values,
    const std::vector<PolyMatrixNtt>& rlwe_cts, size_t num_cts,
    const std::vector<PolyMatrixNtt>& pub_params,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants) {
  YACL_ENFORCE_EQ(b_values.size(), num_cts);
  YACL_ENFORCE_EQ(rlwe_cts.size(), num_cts);

  size_t ell = params.PolyLenLog2();
  auto out =
      RingPackLwesInner(params, ell, 0, rlwe_cts, pub_params, y_constants);

  auto out_raw = FromNtt(params, out);
  for (size_t z = 0; z < params.PolyLen(); ++z) {
    uint128_t b_val_u128 = static_cast<uint128_t>(b_values[z]);
    uint128_t poly_len_u128 = static_cast<uint128_t>(params.PolyLen());
    uint128_t prod = b_val_u128 * poly_len_u128;
    uint64_t val = arith::BarrettReductionU128(params, prod);

    size_t idx = out_raw.PolyStartIndex(1, 0) + z;
    uint64_t sum = out_raw.Data()[idx] + val;
    out_raw.Data()[idx] = arith::BarrettU64(params, sum);
  }

  return ToNtt(params, out_raw);
}

std::vector<uint64_t> NegacyclicPerm(absl::Span<const uint64_t> a, size_t shift,
                                     uint64_t modulus) {
  size_t n = a.size();
  std::vector<uint64_t> out(n);

  for (size_t i = 0; i <= shift; ++i) {
    out[i] = a[shift - i];
  }

  for (size_t i = shift + 1; i < n; ++i) {
    size_t src_idx = n - (i - shift);
    uint64_t val = a[src_idx] % modulus;
    if (val == 0) {
      out[i] = 0;
    } else {
      out[i] = modulus - val;
    }
  }

  return out;
}

std::vector<uint64_t> NegacyclicMatrix(absl::Span<const uint64_t> a,
                                       uint64_t modulus) {
  size_t n = a.size();
  std::vector<uint64_t> out(n * n, 0);

  for (size_t i = 0; i < n; ++i) {
    std::vector<uint64_t> perm = NegacyclicPerm(a, i, modulus);
    for (size_t j = 0; j < n; ++j) {
      out[j * n + i] = perm[j];
    }
  }

  return out;
}

PolyMatrixNtt CondenseMatrix(const Params& params, const PolyMatrixNtt& a) {
  PolyMatrixNtt res = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(),
                                          a.Rows(), a.Cols());
  for (size_t i = 0; i < a.Rows(); ++i) {
    for (size_t j = 0; j < a.Cols(); ++j) {
      auto res_poly = res.Poly(i, j);
      const auto a_poly = a.Poly(i, j);
      if (a_poly.size() < 2 * params.PolyLen()) {
        continue;
      }
      for (size_t z = 0; z < params.PolyLen(); ++z) {
        res_poly[z] = a_poly[z] | (a_poly[z + params.PolyLen()] << 32);
      }
    }
  }

  return res;
}

// from ypir/packing.rs
PolyMatrixNtt RotationPoly(const Params& params, size_t amount) {
  PolyMatrixRaw res = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);

  if (amount < res.Data().size()) {
    res.Data()[amount] = 1;
  } else {
    YACL_THROW("Rotation amount exceeds polynomial length");
  }

  return ToNtt(params, res);
}
PolyMatrixNtt PackSingleLwe(const Params& params,
                            const std::vector<PolyMatrixNtt>& pub_params,
                            const PolyMatrixNtt& lwe_ct) {
  PolyMatrixNtt cur_r = lwe_ct;

  size_t log_n = params.PolyLenLog2();

  for (size_t i = 0; i < log_n; ++i) {
    size_t t = (params.PolyLen() / (1ULL << i)) + 1;

    const auto& pub_param = pub_params[i];

    auto tau_of_r =
        HomomorphicAutomorph(params, t, params.TExpLeft(), cur_r, pub_param);
    AddInto(params, cur_r, tau_of_r);
  }

  return cur_r;
}

PolyMatrixNtt PackUsingSingleWithOffset(
    const Params& params, const std::vector<PolyMatrixNtt>& pub_params,
    const std::vector<PolyMatrixNtt>& cts, size_t offset) {
  PolyMatrixNtt res =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);

  SPDLOG_INFO(
      "PackUsingSingleWithOffset: offset={}, cts.size()={}, poly_len={}",
      offset, cts.size(), params.PolyLen());

  for (size_t i = 0; i < cts.size(); ++i) {
    PolyMatrixNtt packed_single = PackSingleLwe(params, pub_params, cts[i]);

    size_t rotation_amount = offset + i;
    if (rotation_amount >= params.PolyLen()) {
      SPDLOG_ERROR("ERROR: rotation_amount {} >= poly_len {} (offset={}, i={})",
                   rotation_amount, params.PolyLen(), offset, i);
      YACL_THROW(
          "Rotation amount exceeds polynomial length in "
          "PackUsingSingleWithOffset");
    }

    PolyMatrixNtt rotation = RotationPoly(params, rotation_amount);
    PolyMatrixNtt rotated = ScalarMultiply(params, rotation, packed_single);

    AddInto(params, res, rotated);
  }

  return res;
}

std::vector<PolyMatrixNtt> PrepPackLwes(const Params& params,
                                        absl::Span<const uint64_t> lwe_cts,
                                        size_t cols_to_do) {
  size_t poly_len = params.PolyLen();
  size_t expected_size = poly_len * (poly_len + 1);

  if (lwe_cts.size() != expected_size) {
    throw std::runtime_error("PrepPackLwes: lwe_cts size mismatch");
  }
  if (cols_to_do != poly_len) {
    throw std::runtime_error("PrepPackLwes: cols_to_do must equal poly_len");
  }

  std::vector<PolyMatrixNtt> rlwe_cts;
  rlwe_cts.reserve(cols_to_do);

  for (size_t i = 0; i < cols_to_do; ++i) {
    auto rlwe_ct = PolyMatrixRaw::Zero(params.PolyLen(), 2, 1);
    std::vector<uint64_t> poly;
    poly.reserve(poly_len);
    for (size_t j = 0; j < poly_len; ++j) {
      poly.push_back(lwe_cts[j * poly_len + i]);
    }

    auto nega = NegacyclicPerm(poly, 0, params.Modulus());

    // Use Poly(0, 0) to get the first polynomial
    auto first_poly = rlwe_ct.Poly(0, 0);
    for (size_t j = 0; j < poly_len; ++j) {
      first_poly[j] = nega[j];
    }

    rlwe_cts.push_back(ToNtt(params, rlwe_ct));
  }

  return rlwe_cts;
}

std::vector<std::vector<PolyMatrixNtt>> PrepPackManyLwes(
    const Params& params, absl::Span<const uint64_t> lwe_cts,
    size_t num_rlwe_outputs) {
  size_t poly_len = params.PolyLen();

  size_t expected_total_size = (poly_len + 1) * (num_rlwe_outputs * poly_len);

  if (lwe_cts.size() != expected_total_size) {
    throw std::runtime_error("PrepPackManyLwes: Total size mismatch");
  }

  std::vector<std::vector<uint64_t>> vecs;
  vecs.reserve(num_rlwe_outputs);

  for (size_t i = 0; i < num_rlwe_outputs; ++i) {
    std::vector<uint64_t> v;
    // 每个 chunk 的大小应该是 poly_len * (poly_len + 1)
    v.reserve(poly_len * (poly_len + 1));
    for (size_t j = 0; j < poly_len + 1; ++j) {
      size_t start_idx = j * (num_rlwe_outputs * poly_len) + i * poly_len;
      for (size_t k = 0; k < poly_len; ++k) {
        v.push_back(lwe_cts[start_idx + k]);
      }
    }
    vecs.push_back(std::move(v));
  }
  std::vector<std::vector<PolyMatrixNtt>> res;
  res.reserve(num_rlwe_outputs);

  for (size_t i = 0; i < num_rlwe_outputs; ++i) {
    res.push_back(PrepPackLwes(params, vecs[i], poly_len));
  }

  return res;
}

std::vector<std::vector<uint64_t>> GenerateAutomorphTablesBruteForce(
    const Params& params) {
  std::vector<std::vector<uint64_t>> tables;

  tables.reserve(params.PolyLenLog2());

  for (size_t i = params.PolyLenLog2(); i >= 1; --i) {
    size_t poly_len = params.PolyLen();
    std::vector<uint64_t> table_candidate(poly_len);
    while (true) {
      uint64_t t = (1ULL << i) + 1;
      PolyMatrixRaw poly = PolyMatrixRaw::Random(params, 1, 1);
      PolyMatrixNtt poly_ntt = ToNtt(params, poly);
      PolyMatrixRaw poly_auto(params.PolyLen(), 1, 1);
      Automorphism(params, poly_auto, poly, t);
      PolyMatrixNtt poly_auto_ntt = ToNtt(params, poly_auto);
      auto pol_orig = poly_ntt.Poly(0, 0);
      auto pol_auto = poly_auto_ntt.Poly(0, 0);
      bool must_redo = false;
      for (size_t src_idx = 0; src_idx < poly_len; ++src_idx) {
        uint64_t target_val = pol_orig[src_idx];
        int found_count = 0;
        size_t found_pos = 0;
        for (size_t dest_idx = 0; dest_idx < poly_len; ++dest_idx) {
          if (pol_auto[dest_idx] == target_val) {
            found_count++;
            found_pos = dest_idx;
          }
        }
        if (found_count != 1) {
          must_redo = true;
          break;
        }
        table_candidate[found_pos] = src_idx;
      }

      if (!must_redo) {
        break;
      }
    }
    tables.push_back(std::move(table_candidate));
  }

  return tables;
}

std::tuple<PolyMatrixNtt, std::vector<PolyMatrixNtt>,
           std::vector<std::vector<uint64_t>>>
PrecomputePack(const Params& params, size_t poly_len_log2,
               const std::vector<PolyMatrixNtt>& prepacked,
               const std::vector<PolyMatrixNtt>& pub_params,
               const std::pair<std::vector<PolyMatrixNtt>,
                               std::vector<PolyMatrixNtt>>& y_constants) {
  YACL_ENFORCE_EQ(pub_params.size(), params.PolyLenLog2(),
                  "pub_params size must match poly_len_log2");
  YACL_ENFORCE_EQ(params.CrtCount(), 2UL, "CRT count must be 2");

  std::vector<PolyMatrixNtt> working_set = prepacked;
  PolyMatrixNtt y_times_ct_odd =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);

  PolyMatrixNtt neg_y_times_ct_odd =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);
  PolyMatrixNtt ct_sum_1 =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);

  PolyMatrixRaw ct_raw = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  PolyMatrixRaw ct_auto = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);

  size_t t_exp = params.TExpLeft();
  PolyMatrixRaw ginv_ct =
      PolyMatrixRaw::Zero(params.PolyLen(), t_exp, 1);  // Fixed: poly_len first
  PolyMatrixNtt ginv_ct_ntt =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), t_exp, 1);

  PolyMatrixNtt ct_auto_1_ntt =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1);
  PolyMatrixNtt w_times_ginv_ct =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);
  PolyMatrixNtt scratch =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);
  std::vector<PolyMatrixNtt> v_trgsw;
  v_trgsw.reserve(poly_len_log2);

  for (size_t cur_ell = 1; cur_ell <= poly_len_log2; ++cur_ell) {
    size_t num_in = 1ULL << (poly_len_log2 - cur_ell + 1);
    size_t num_out = num_in >> 1;

    const auto& y = y_constants.first[cur_ell - 1];
    const auto& neg_y = y_constants.second[cur_ell - 1];

    for (size_t i = 0; i < num_out; ++i) {
      auto& ct_even = working_set[i];
      const auto& ct_odd = working_set[i + num_out];

      // y_times_ct_odd = y * ct_odd
      ScalarMultiply(params, y_times_ct_odd, y, ct_odd);
      // neg_y_times_ct_odd = -y * ct_odd
      ScalarMultiply(params, neg_y_times_ct_odd, neg_y, ct_odd);

      // ct_sum_1 = ct_even + neg_y * ct_odd
      ct_sum_1.CopyInto(ct_even, 0, 0);
      AddInto(params, ct_sum_1, neg_y_times_ct_odd);

      AddInto(params, ct_even, y_times_ct_odd);

      uint64_t t = (1ULL << cur_ell) + 1;
      FromNttScratch(params, ct_raw, scratch.Data(), ct_sum_1);

      Automorphism(params, ct_auto, ct_raw, t);
      util::GadgetInvertRdim(params, ginv_ct, ct_auto, 1);
      ToNtt(params, ginv_ct_ntt, ginv_ct);

      auto scratch_poly = scratch.Poly(0, 0);  // Get the stored 2nd row data
      AutomorphismPolyUncrtd(params, ct_auto_1_ntt.Data(), scratch_poly, t);
      arith::NttForward(params, ct_auto_1_ntt.Data());

      v_trgsw.push_back(CondenseMatrix(params, ginv_ct_ntt));

      size_t param_idx = poly_len_log2 - cur_ell;
      const auto& pub_param = pub_params[param_idx];

      // W * Ginv
      std::fill(w_times_ginv_ct.Data().begin(), w_times_ginv_ct.Data().end(),
                0);
      MultiplyNoReduce(w_times_ginv_ct, pub_param, ginv_ct_ntt, 0);

      AddIntoAt(params, ct_even, ct_auto_1_ntt, 1, 0);

      AddInto(params, ct_even, w_times_ginv_ct);
    }
  }

  auto combined_hints = GenerateAutomorphTablesBruteForce(params);
  return std::make_tuple(working_set[0], v_trgsw, combined_hints);
}

inline uint64_t FastBarrettRawU64(uint64_t input, uint64_t const_ratio_1,
                                  uint64_t modulus) {
  uint128_t mul = static_cast<uint128_t>(input) * const_ratio_1;
  uint64_t tmp = static_cast<uint64_t>(mul >> 64);
  uint64_t res = input - tmp * modulus;
  return res;
}

void FastAddInto(const Params& params, PolyMatrixNtt& res,
                 const PolyMatrixNtt& a) {
  if (res.Rows() != a.Rows() || res.Cols() != a.Cols()) {
    throw std::runtime_error("FastAddInto: Dimension mismatch");
  }

  size_t poly_len = params.PolyLen();
  size_t crt_count = params.CrtCount();

  uint64_t* res_ptr = const_cast<uint64_t*>(res.Data().data());
  const uint64_t* a_ptr = a.Data().data();
  size_t num_polys = res.Rows() * res.Cols();

  for (size_t p = 0; p < num_polys; ++p) {
    for (size_t c = 0; c < crt_count; ++c) {
      size_t poly_offset = p * (crt_count * poly_len);
      size_t crt_offset = poly_offset + c * poly_len;

      for (size_t i = 0; i < poly_len; ++i) {
        size_t idx = crt_offset + i;
        uint64_t val = res_ptr[idx] + a_ptr[idx];
        res_ptr[idx] =
            FastBarrettRawU64(val, params.BarrettCr1(c), params.Moduli(c));
      }
    }
  }
}

void FastAddIntoNoReduce(PolyMatrixNtt& res, const PolyMatrixNtt& a) {
  if (res.Rows() != a.Rows() || res.Cols() != a.Cols()) {
    throw std::runtime_error("FastAddIntoNoReduce: Dimension mismatch");
  }
  size_t len = res.Data().size();
  for (size_t i = 0; i < len; ++i) {
    res.Data()[i] += a.Data()[i];
  }
}

void MultiplyPolyAvx(uint64_t* res, const uint64_t* a, const uint64_t* b,
                     size_t len) {
  for (size_t i = 0; i < len; ++i) {
    uint64_t x = static_cast<uint64_t>(static_cast<uint32_t>(a[i]));
    uint64_t y = static_cast<uint64_t>(static_cast<uint32_t>(b[i]));
    res[i] = x * y;
  }
}

void ScalarMultiplyAvx(const Params& params, PolyMatrixNtt& res,
                       const PolyMatrixNtt& a, const PolyMatrixNtt& b) {
  if (a.Rows() != 1 || a.Cols() != 1) {
    throw std::runtime_error(
        "ScalarMultiplyAvx: 'a' must be a 1x1 matrix (scalar)");
  }

  if (res.Rows() != b.Rows() || res.Cols() != b.Cols()) {
    throw std::runtime_error(
        "ScalarMultiplyAvx: 'res' and 'b' dimension mismatch");
  }

  // poly_size = poly_len * crt_count
  size_t poly_size = params.PolyLen() * params.CrtCount();

  const uint64_t* pol2_ptr = a.Data().data();

  const uint64_t* b_ptr = b.Data().data();

  uint64_t* res_ptr = const_cast<uint64_t*>(res.Data().data());

  size_t total_polys = b.Rows() * b.Cols();

  for (size_t i = 0; i < total_polys; ++i) {
    size_t offset = i * poly_size;

    const uint64_t* pol1_ptr = b_ptr + offset;

    uint64_t* res_poly_ptr = res_ptr + offset;

    MultiplyPolyAvx(res_poly_ptr, pol1_ptr, pol2_ptr, poly_size);
  }
}

void FastMultiplyNoReduce(const Params& params, PolyMatrixNtt& res,
                          const PolyMatrixNtt& a, const PolyMatrixNtt& b,
                          size_t /*start_inner_dim*/) {
  if (res.Rows() != 1 || res.Cols() != 1) {
    throw std::runtime_error("FastMultiplyNoReduce: Result must be 1x1");
  }
  if (res.Rows() != a.Rows() || res.Cols() != b.Cols()) {
    throw std::runtime_error(
        "FastMultiplyNoReduce: Dimension mismatch (Result)");
  }
  if (a.Cols() != b.Rows()) {
    throw std::runtime_error(
        "FastMultiplyNoReduce: Dimension mismatch (Inner)");
  }

  if (params.CrtCount() != 2) {
    throw std::runtime_error("FastMultiplyNoReduce: Requires CRT count = 2");
  }

  uint64_t* res_ptr = const_cast<uint64_t*>(res.Data().data());
  const uint64_t* a_ptr = a.Data().data();
  const uint64_t* b_ptr = b.Data().data();

  size_t pol_sz = params.PolyLen();
  size_t k_dim = a.Cols();

  for (size_t idx = 0; idx < pol_sz; ++idx) {
    uint64_t sum_lo = 0;
    uint64_t sum_hi = 0;

    for (size_t k = 0; k < k_dim; ++k) {
      size_t offset = k * 2 * pol_sz + idx;

      uint64_t x = a_ptr[offset];
      uint64_t y = b_ptr[offset];

      uint64_t x_lo = x & 0xFFFFFFFF;
      uint64_t x_hi = x >> 32;

      uint64_t y_lo = y & 0xFFFFFFFF;
      uint64_t y_hi = y >> 32;

      sum_lo += x_lo * y_lo;
      sum_hi += x_hi * y_hi;
    }

    res_ptr[idx] = sum_lo;
    res_ptr[pol_sz + idx] = sum_hi;
  }
}

void ApplyAutomorphNttRaw(const Params& params, const uint64_t* poly,
                          uint64_t* out, size_t t,
                          const std::vector<std::vector<size_t>>& tables) {
  size_t poly_len = params.PolyLen();
  if (t <= 1) throw std::runtime_error("ApplyAutomorphNttRaw: t must be > 1");

  size_t val = poly_len / (t - 1);
  // C++20
  // size_t table_idx = std::countr_zero(val);
  size_t table_idx = __builtin_ctzll(val);

  if (table_idx >= tables.size()) {
    throw std::runtime_error("ApplyAutomorphNttRaw: Table index out of bounds");
  }

  const std::vector<size_t>& table = tables[table_idx];

  for (size_t i = 0; i < poly_len; ++i) {
    out[i] += poly[table[i]];
  }
}

void ApplyAutomorphNtt(const Params& params,
                       const std::vector<std::vector<size_t>>& tables,
                       const PolyMatrixNtt& mat, PolyMatrixNtt& res, size_t t) {
  if (mat.Rows() != res.Rows() || mat.Cols() != res.Cols()) {
    throw std::runtime_error("ApplyAutomorphNtt: Dimension mismatch");
  }

  const uint64_t* mat_ptr = mat.Data().data();

  uint64_t* res_ptr = const_cast<uint64_t*>(res.Data().data());

  size_t poly_len = params.PolyLen();
  size_t total_elements = mat.Data().size();

  for (size_t offset = 0; offset < total_elements; offset += poly_len) {
    ApplyAutomorphNttRaw(params, mat_ptr + offset, res_ptr + offset, t, tables);
  }
}

void FastReduce(const Params& params, PolyMatrixNtt& res) {
  size_t crt_count = params.CrtCount();
  size_t poly_len = params.PolyLen();

  for (size_t m = 0; m < crt_count; ++m) {
    for (size_t i = 0; i < poly_len; ++i) {
      size_t idx = m * poly_len + i;
      res.Data()[idx] = arith::BarrettCoeffU64(params, res.Data()[idx], m);
    }
  }
}

PolyMatrixNtt PackUsingPrecompVals(
    const Params& params, size_t ell,
    absl::Span<const PolyMatrixNtt> pub_params,
    absl::Span<const uint64_t> b_values, const PolyMatrixNtt& precomp_res,
    absl::Span<const PolyMatrixNtt> precomp_vals,
    const std::vector<std::vector<size_t>>& precomp_tables,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants) {
  size_t initial_capacity = 1ULL << (ell - 1);
  std::vector<PolyMatrixNtt> working_set;
  working_set.reserve(initial_capacity);

  for (size_t i = 0; i < initial_capacity; ++i) {
    working_set.push_back(
        PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1));
  }

  auto y_times_ct_odd =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1);
  auto neg_y_times_ct_odd =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1);
  auto ct_sum_1 =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1);
  auto w_times_ginv_ct =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 1, 1);

  size_t idx_precomp = 0;

  for (size_t cur_ell = 1; cur_ell <= ell; ++cur_ell) {
    size_t num_in = 1ULL << (ell - cur_ell + 1);
    size_t num_out = num_in >> 1;

    if (num_in == params.PolyLen()) {
      num_in = num_out;
    }

    for (size_t i = 0; i < num_out; ++i) {
      PolyMatrixNtt& ct_even = working_set[i];

      const auto& y = y_constants.first[cur_ell - 1];
      const auto& neg_y = y_constants.second[cur_ell - 1];

      if (cur_ell > 1) {
        PolyMatrixNtt& ct_odd = working_set[num_out + i];
        ScalarMultiplyAvx(params, y_times_ct_odd, y, ct_odd);
        ScalarMultiplyAvx(params, neg_y_times_ct_odd, neg_y, ct_odd);
      }

      if (cur_ell > 1) {
        std::copy(ct_even.Data().begin(), ct_even.Data().end(),
                  const_cast<uint64_t*>(ct_sum_1.Data().data()));

        FastAddIntoNoReduce(ct_sum_1, neg_y_times_ct_odd);
        FastAddIntoNoReduce(ct_even, y_times_ct_odd);
      }

      const PolyMatrixNtt* ct_ptr = &ct_sum_1;
      size_t t = (1ULL << cur_ell) + 1;

      const auto& cur_ginv_ct_ntt = precomp_vals[idx_precomp];
      idx_precomp++;

      size_t w_idx = params.PolyLenLog2() - cur_ell;
      const auto& w = pub_params[w_idx];

      FastMultiplyNoReduce(params, w_times_ginv_ct, w, cur_ginv_ct_ntt, 0);

      if (cur_ell > 1) {
        ApplyAutomorphNtt(params, precomp_tables, *ct_ptr, ct_even, t);

        if (i < num_out / 2 && ((cur_ell - 1) % 5 != 0)) {
          FastAddIntoNoReduce(ct_even, w_times_ginv_ct);
        } else {
          FastAddInto(params, ct_even, w_times_ginv_ct);
        }
      } else {
        if (i < num_out / 2) {
          FastAddIntoNoReduce(ct_even, w_times_ginv_ct);
        } else {
          FastAddInto(params, ct_even, w_times_ginv_ct);
        }
      }
    }
  }

  if (idx_precomp != precomp_vals.size()) {
    throw std::runtime_error("PackUsingPrecompVals: idx_precomp mismatch");
  }

  PolyMatrixNtt resulting_row_1 = working_set[0];
  FastReduce(params, resulting_row_1);

  PolyMatrixNtt res = precomp_res;

  size_t poly_len = params.PolyLen();
  size_t crt_count = params.CrtCount();
  size_t row_1_offset_ntt = crt_count * poly_len;

  uint64_t* res_data = const_cast<uint64_t*>(res.Data().data());
  const uint64_t* row_1_src = resulting_row_1.Data().data();

  std::copy(row_1_src, row_1_src + (crt_count * poly_len),
            res_data + row_1_offset_ntt);

  PolyMatrixRaw out_raw = FromNtt(params, res);
  uint64_t modulus = params.Modulus();

  for (size_t z = 0; z < poly_len; ++z) {
    uint128_t val_u128 = static_cast<uint128_t>(b_values[z]) * poly_len;
    uint64_t val = arith::BarrettReductionU128(params, val_u128);

    size_t idx = poly_len + z;

    out_raw.Data()[idx] += val;
    if (out_raw.Data()[idx] >= modulus) {
      out_raw.Data()[idx] -= modulus;
    }
  }

  return ToNtt(params, out_raw);
}

std::vector<PolyMatrixNtt> PackManyLwes(
    const Params& params,
    const std::vector<std::vector<PolyMatrixNtt>>& prep_rlwe_cts,
    const Precomp& precomp, absl::Span<const uint64_t> b_values,
    size_t num_rlwe_outputs,
    const std::vector<PolyMatrixNtt>& pack_pub_params_row_1s,
    const std::pair<std::vector<PolyMatrixNtt>, std::vector<PolyMatrixNtt>>&
        y_constants) {
  if (prep_rlwe_cts.size() != num_rlwe_outputs) {
    throw std::runtime_error("PackManyLwes: prep_rlwe_cts size mismatch");
  }

  if (!prep_rlwe_cts.empty() && prep_rlwe_cts[0].size() != params.PolyLen()) {
    throw std::runtime_error("PackManyLwes: prep_rlwe_cts inner size mismatch");
  }

  if (b_values.size() != num_rlwe_outputs * params.PolyLen()) {
    throw std::runtime_error("PackManyLwes: b_values size mismatch");
  }

  if (precomp.size() != num_rlwe_outputs) {
    throw std::runtime_error("PackManyLwes: precomp size mismatch");
  }

  std::vector<PolyMatrixNtt> res;
  res.reserve(num_rlwe_outputs);

  size_t poly_len = params.PolyLen();
  size_t poly_len_log2 = params.PolyLenLog2();

  for (size_t i = 0; i < num_rlwe_outputs; ++i) {
    const auto& [precomp_res, precomp_vals, precomp_tables] = precomp[i];

    auto b_values_slice = b_values.subspan(i * poly_len, poly_len);

    PolyMatrixNtt packed = PackUsingPrecompVals(
        params, poly_len_log2,
        absl::MakeConstSpan(pack_pub_params_row_1s),  // Vector -> Span
        b_values_slice, precomp_res,
        absl::MakeConstSpan(precomp_vals),  // Vector -> Span
        precomp_tables, y_constants);

    res.push_back(std::move(packed));
  }

  return res;
}

PolyMatrixNtt GetRegevSample(const Params& params, const PolyMatrixRaw& sk_reg,
                             yacl::crypto::Prg<uint64_t>& rng,
                             yacl::crypto::Prg<uint64_t>& rng_pub) {
  auto a = PolyMatrixRaw::RandomPrg(params, 1, 1, rng_pub);
  auto a_ntt = ToNtt(params, a);
  auto a_inv = ToNtt(params, Negate(params, a));
  DiscreteGaussian dg(params.NoiseWidth());
  auto e = Noise(params, 1, 1, dg, rng);

  auto e_ntt = ToNtt(params, e);
  auto sk_reg_ntt = ToNtt(params, sk_reg);
  auto b_p = Multiply(params, sk_reg_ntt, a_ntt);
  auto b = Add(params, e_ntt, b_p);
  auto p = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);
  p.CopyInto(a_inv, 0, 0);
  p.CopyInto(b, 1, 0);

  return p;
}

PolyMatrixNtt GetFreshRegevPublicKey(const Params& params,
                                     const PolyMatrixRaw& sk_reg, size_t m,
                                     yacl::crypto::Prg<uint64_t>& rng,
                                     yacl::crypto::Prg<uint64_t>& rng_pub) {
  auto p = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, m);
  for (size_t i = 0; i < m; ++i) {
    p.CopyInto(GetRegevSample(params, sk_reg, rng, rng_pub), 0, i);
  }
  return p;
}

std::vector<PolyMatrixNtt> RawGenerateExpansionParams(
    const Params& params, const PolyMatrixRaw& sk_reg, size_t num_exp,
    size_t m_exp, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) {
  auto g_exp = util::BuildGadget(params, 1, m_exp);
  auto g_exp_ntt = ToNtt(params, g_exp);

  std::vector<PolyMatrixNtt> res;
  res.reserve(num_exp);

  for (size_t i = 0; i < num_exp; ++i) {
    size_t t = (params.PolyLen() / (1ULL << i)) + 1;
    auto tau_sk_reg = Automorphism(params, sk_reg, t);
    auto tau_sk_reg_ntt = ToNtt(params, tau_sk_reg);
    auto prod = Multiply(params, tau_sk_reg_ntt, g_exp_ntt);
    auto sample = GetFreshRegevPublicKey(params, sk_reg, m_exp, rng, rng_pub);

    PolyMatrixNtt padded_prod = prod.PadTop(1);
    auto w_exp_i = Add(params, sample, padded_prod);
    res.push_back(std::move(w_exp_i));
  }
  return res;
}

// from kernal.rs

template <typename T>
void FastBatchedDotProduct(const Params& params, uint64_t* c, const uint64_t* a,
                           size_t a_elems, const T* b_t, size_t b_rows,
                           size_t b_cols) {
  if (a_elems != b_rows) {
    throw std::runtime_error("FastBatchedDotProduct: Dimension mismatch");
  }

  const size_t chunk_size = std::min<size_t>(65536, a_elems);
  const size_t num_chunks = a_elems / chunk_size;

  for (size_t k_outer = 0; k_outer < num_chunks; ++k_outer) {
    const uint64_t* a_chunk_ptr = a + (k_outer * chunk_size);
    for (size_t j = 0; j < b_cols; ++j) {
      const T* b_ptr = b_t + (j * b_rows) + (k_outer * chunk_size);
      uint64_t total_sum_lo = 0;
      uint64_t total_sum_hi = 0;
      for (size_t k_inner = 0; k_inner < chunk_size; ++k_inner) {
        uint64_t a_val = a_chunk_ptr[k_inner];
        uint64_t b_val = static_cast<uint64_t>(b_ptr[k_inner]);
        uint64_t a_lo = a_val & 0xFFFFFFFF;
        uint64_t a_hi = a_val >> 32;
        total_sum_lo += a_lo * b_val;
        total_sum_hi += a_hi * b_val;
      }

      uint64_t lo = arith::BarrettCoeffU64(params, total_sum_lo, 0);
      uint64_t hi = arith::BarrettCoeffU64(params, total_sum_hi, 1);

      uint64_t res = params.CrtCompose2(lo, hi);

      uint64_t sum = c[j] + res;
      c[j] = arith::BarrettU64(params, sum);
    }
  }
}

// from serialize.rs
std::vector<PolyMatrixNtt> UnpackVecPm(const Params& params, size_t rows,
                                       size_t cols,
                                       absl::Span<const uint64_t> data) {
  if (params.CrtCount() != 2) {
    throw std::runtime_error("UnpackVecPm: Params CRT count must be 2");
  }

  size_t poly_len = params.PolyLen();
  size_t chunk_size = rows * cols * poly_len;

  if (chunk_size == 0) {
    return {};
  }

  if (data.size() % chunk_size != 0) {
    throw std::runtime_error(
        "UnpackVecPm: Data size not aligned with matrix dimensions");
  }

  size_t num_matrices = data.size() / chunk_size;
  std::vector<PolyMatrixNtt> v_cts;
  v_cts.reserve(num_matrices);

  for (size_t i = 0; i < num_matrices; ++i) {
    const uint64_t* in_data_ptr = data.data() + (i * chunk_size);

    auto ct =
        PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), rows, cols);

    uint64_t* out_data_ptr = const_cast<uint64_t*>(ct.Data().data());

    for (size_t row = 0; row < rows; ++row) {
      for (size_t col = 0; col < cols; ++col) {
        size_t in_offs = (row * cols + col) * poly_len;

        size_t out_offs = (row * cols + col) * 2 * poly_len;

        for (size_t z = 0; z < poly_len; ++z) {
          out_data_ptr[out_offs + z] = in_data_ptr[in_offs + z];
        }
      }
    }
    v_cts.push_back(std::move(ct));
  }

  return v_cts;
}
PolyMatrixNtt UncondenseMatrix(const Params& params, const PolyMatrixNtt& a) {
  size_t rows = a.Rows();
  size_t cols = a.Cols();
  size_t poly_len = params.PolyLen();

  PolyMatrixNtt res =
      PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), rows, cols);

  for (size_t i = 0; i < rows; ++i) {
    for (size_t j = 0; j < cols; ++j) {
      auto res_poly = res.Poly(i, j);
      const auto a_poly = a.Poly(i, j);

      for (size_t z = 0; z < poly_len; ++z) {
        uint64_t val = a_poly[z];
        res_poly[z] = val & 0xFFFFFFFFULL;
        res_poly[z + poly_len] = val >> 32;
      }
    }
  }

  return res;
}
// from modulus switch.rs
std::vector<uint8_t> ModulusSwitch(const Params& params,
                                   const PolyMatrixRaw& poly_matrix,
                                   uint64_t q_prime_1, uint64_t q_prime_2) {
  if (poly_matrix.Rows() != 2 || poly_matrix.Cols() != 1) {
    throw std::runtime_error("ModulusSwitch: Matrix must be 2x1");
  }
  size_t poly_len = params.PolyLen();
  uint64_t current_modulus = params.Modulus();

  size_t bits_for_row0 = static_cast<size_t>(std::ceil(std::log2(q_prime_2)));
  size_t bits_for_row1 = static_cast<size_t>(std::ceil(std::log2(q_prime_1)));

  size_t total_sz_bits = (bits_for_row0 + bits_for_row1) * poly_len;
  size_t total_sz_bytes = (total_sz_bits + 7) / 8;

  std::vector<uint8_t> res(total_sz_bytes, 0);

  size_t bit_offs = 0;

  const uint64_t* row_0_ptr = poly_matrix.Data().data();

  for (size_t z = 0; z < poly_len; ++z) {
    uint64_t val = row_0_ptr[z];
    uint64_t val_rescaled = arith::Rescale(val, current_modulus, q_prime_2);

    util::WriteArbitraryBits(res, val_rescaled, bit_offs, bits_for_row0);
    bit_offs += bits_for_row0;
  }

  const uint64_t* row_1_ptr = poly_matrix.Data().data() + poly_len;

  for (size_t z = 0; z < poly_len; ++z) {
    uint64_t val = row_1_ptr[z];
    uint64_t val_rescaled = arith::Rescale(val, current_modulus, q_prime_1);

    util::WriteArbitraryBits(res, val_rescaled, bit_offs, bits_for_row1);
    bit_offs += bits_for_row1;
  }

  return res;
}

// Explicit instantiation of template functions
template void FastBatchedDotProduct<uint8_t>(const Params& params, uint64_t* c,
                                             const uint64_t* a, size_t a_elems,
                                             const uint8_t* b_t, size_t b_rows,
                                             size_t b_cols);
template void FastBatchedDotProduct<uint16_t>(const Params& params, uint64_t* c,
                                              const uint64_t* a, size_t a_elems,
                                              const uint16_t* b_t,
                                              size_t b_rows, size_t b_cols);
template void FastBatchedDotProduct<uint32_t>(const Params& params, uint64_t* c,
                                              const uint64_t* a, size_t a_elems,
                                              const uint32_t* b_t,
                                              size_t b_rows, size_t b_cols);

}  // namespace psi::ypir
