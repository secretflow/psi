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

#include "psi/algorithm/spiral/spiral_server.h"

#include <algorithm>

#include "spdlog/spdlog.h"
#include "yacl/utils/elapsed_timer.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/arith/ntt.h"
#include "psi/algorithm/spiral/common.h"
#include "psi/algorithm/spiral/gadget.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {
std::vector<uint64_t> SpiralServer::ReorientRawDb(
    const std::vector<std::vector<uint8_t>>& raw_database) {
  // first, we convert raw_database into plaintexts in SpiralPIR, just R_q^{n*n}
  std::vector<uint8_t> combined_bytes;
  for (const auto& raw : raw_database) {
    combined_bytes.insert(combined_bytes.end(), raw.begin(), raw.end());
  }

  YACL_ENFORCE_EQ(raw_database.size(), database_info_.rows_);
  YACL_ENFORCE_LE(raw_database[0].size(), params_.MaxByteLenOfPt());

  // here, we only consider one row of raw data can be holded by one plaintext
  // in SpiralPIR
  size_t element_byte_len =
      std::min(database_info_.byte_size_per_row_, params_.MaxByteLenOfPt());
  // the number of one plaintext can hold the rows in raw database
  size_t element_size_of_pt = params_.ElementSizeOfPt(element_byte_len);
  // we can conmpute how many plaintexts can be used to hold the whole raw
  // database
  size_t plaintext_size =
      arith::UintNum(raw_database.size(), element_size_of_pt);
  // now we convert raw data to plaintext
  // the total plaintext size
  size_t prod = 1 << (params_.DbDim1() + params_.DbDim2());
  // one Plaintext can hold how many bytes
  size_t byte_size_of_pt = element_size_of_pt * element_byte_len;
  // the total bytes of database
  size_t total_byte_size = database_info_.rows_ * element_byte_len;
  // (one plaintext can hold the number of rows) * (one pt provided the coeff
  // nums for one row)
  size_t used_coeff_size =
      element_size_of_pt *
      arith::UintNum(8 * element_byte_len, params_.PtModulusBitLen());

  YACL_ENFORCE_LE(used_coeff_size,
                  params_.N() * params_.N() * params_.PolyLen());

  size_t offset = 0;

  std::vector<std::vector<uint64_t>> coeff_vec;

  for (size_t i = 0; i < plaintext_size; ++i) {
    size_t process_byte_size = 0;
    if (total_byte_size <= offset) {
      break;
    } else if (total_byte_size < offset + byte_size_of_pt) {
      process_byte_size = total_byte_size - offset;
    } else {
      process_byte_size = byte_size_of_pt;
    }
    YACL_ENFORCE(process_byte_size % element_byte_len == 0);

    auto coeffs = util::ConvertBytesToCoeffs(params_.PtModulusBitLen(), offset,
                                             process_byte_size, combined_bytes);
    YACL_ENFORCE_LE(coeffs.size(), used_coeff_size);
    offset += process_byte_size;

    // padding coeffs
    // we need the coeffs length will be n^2 * len
    while (coeffs.size() < params_.PtCoeffs()) {
      coeffs.push_back(1ULL);
    }
    coeff_vec.push_back(std::move(coeffs));
  }
  //
  size_t cur_plaintext_size = coeff_vec.size();
  YACL_ENFORCE(cur_plaintext_size <= plaintext_size);
  // now padding the size to prod
  while (cur_plaintext_size < prod) {
    std::vector<uint64_t> coeffs(params_.PtCoeffs(), 1ULL);
    coeff_vec.push_back(std::move(coeffs));
    ++cur_plaintext_size;
  }

  // finally, we obtain a coeff_vec, which means plaintexts
  // we need to reorient it
  auto reorient_db = ReorientDatabase(params_, coeff_vec);

  return reorient_db;
}

void SpiralServer::SetDatabase(const pir_utils::RawDatabase& raw_database) {
  size_t partition_byte_len = params_.MaxByteLenOfPt();
  // one Pt can hold one row data
  // do not need to partition
  if (partition_byte_len >= raw_database.RowByteLen()) {
    // reoriented_dbs_.push_back(ReorientRawDb(raw_database.Db()));
    reoriented_dbs_ = ReorientRawDb(raw_database.Db());
    single_db_size_ = reoriented_dbs_.size();
    database_seted_ = true;
    partition_num_ = 1;
    return;
  }
  // now we need to Partition the raw database
  partition_num_ =
      (raw_database.RowByteLen() + partition_byte_len - 1) / partition_byte_len;
  auto sub_dbs = raw_database.Partition(partition_byte_len);

  // now we handle each sub db
  // reoriented_dbs_.resize(partition_num_);
  for (size_t j = 0; j < partition_num_; ++j) {
    std::vector<uint64_t> tmp = ReorientRawDb(sub_dbs[j].Db());

    if (j == 0) {
      reoriented_dbs_.reserve(tmp.size() * partition_num_);
      single_db_size_ = tmp.size();
    }
    YACL_ENFORCE_EQ(single_db_size_, tmp.size());

    reoriented_dbs_.insert(reoriented_dbs_.end(), tmp.begin(), tmp.end());

    // reoriented_dbs_[j] = std::move(ReorientRawDb(sub_dbs[j].Db()));
  }

  database_seted_ = true;
}

void SpiralServer::CoefficientExpansion(
    std::vector<PolyMatrixNtt>& v, size_t g, size_t stop_round,
    const std::vector<PolyMatrixNtt>& v_w_left,
    const std::vector<PolyMatrixNtt>& v_w_right,
    const std::vector<PolyMatrixNtt>& v_neg1,
    size_t max_btis_to_gen_right) const {
  size_t poly_len = params_.PolyLen();

  size_t t_exp_left = params_.TExpLeft();
  size_t t_exp_right = params_.TExpRight();

  for (size_t i = 0; i < g; ++i) {
    size_t num_in = static_cast<size_t>(1) << i;
    size_t t = (poly_len / (1 << i)) + 1;

    const auto& neg1 = v_neg1[i];

    // closure
    auto action_expand = [&](size_t j, PolyMatrixNtt& fj) {
      bool cond1 = stop_round > 0 && i > stop_round && (j % 2) == 1;
      bool cond2 = stop_round > 0 && i == stop_round && (j % 2 == 1) &&
                   ((j / 2) >= max_btis_to_gen_right);

      if (cond1 || cond2) {
        return;
      }
      auto ct = PolyMatrixRaw::Zero(params_.PolyLen(), 2, 1);
      auto ct_auto = PolyMatrixRaw::Zero(params_.PolyLen(), 2, 1);
      auto ct_auto1 = PolyMatrixRaw::Zero(params_.PolyLen(), 1, 1);
      auto ct_auto1_ntt =
          PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 1, 1);
      auto w_times_g_inv_ct =
          PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);

      auto g_inv_ct_left =
          PolyMatrixRaw::Zero(params_.PolyLen(), t_exp_left, 1);
      auto g_inv_ct_left_ntt = PolyMatrixNtt::Zero(
          params_.CrtCount(), params_.PolyLen(), t_exp_left, 1);
      auto g_inv_ct_right =
          PolyMatrixRaw::Zero(params_.PolyLen(), t_exp_right, 1);
      auto g_inv_ct_right_ntt = PolyMatrixNtt::Zero(
          params_.CrtCount(), params_.PolyLen(), t_exp_right, 1);

      const PolyMatrixNtt* w_p{};
      PolyMatrixRaw* gi_ct_p{};
      PolyMatrixNtt* gi_ct_ntt_p{};
      if ((i != 0) && (j % 2 == 0)) {
        WEAK_ENFORCE(i < v_w_left.size());

        w_p = &v_w_left[i];
        gi_ct_p = &g_inv_ct_left;
        gi_ct_ntt_p = &g_inv_ct_left_ntt;
      } else {
        WEAK_ENFORCE(i < v_w_right.size());

        w_p = &v_w_right[i];
        gi_ct_p = &g_inv_ct_right;
        gi_ct_ntt_p = &g_inv_ct_right_ntt;
      }

      auto& w = *w_p;
      auto& gi_ct = *gi_ct_p;
      auto& gi_ct_ntt = *gi_ct_ntt_p;

      FromNtt(params_, ct, fj);
      Automorphism(params_, ct_auto, ct, t);

      util::GadgetInvertRdim(params_, gi_ct, ct_auto, 1);
      ToNttNoReduce(params_, gi_ct_ntt, gi_ct);
      // copy ct_auto to ct_auto1
      std::memcpy(ct_auto1.Data().data(),
                  ct_auto.Data().data() + ct_auto.PolyStartIndex(1, 0),
                  ct_auto1.NumWords() * sizeof(uint64_t));

      ToNtt(params_, ct_auto1_ntt, ct_auto1);
      Multiply(params_, w_times_g_inv_ct, w, gi_ct_ntt);

      size_t idx = 0;
      for (size_t row_idx = 0; row_idx < 2; ++row_idx) {
        for (size_t n = 0; n < params_.CrtCount(); ++n) {
          for (size_t z = 0; z < poly_len; ++z) {
            uint64_t sum = fj.Data()[idx] + w_times_g_inv_ct.Data()[idx] +
                           static_cast<uint64_t>(row_idx) *
                               ct_auto1_ntt.Data()[n * poly_len + z];

            fj.Data()[idx] = arith::BarrettCoeffU64(params_, sum, n);

            idx += 1;
          }
        }
      }
    };  // lambda action_expand end

    // a big improve for performance
    yacl::parallel_for(0, num_in, [&](size_t begin, size_t end) {
      for (size_t j = begin; j < end; ++j) {
        ScalarMultiply(params_, v[j + num_in], neg1, v[j]);
        action_expand(j, v[j]);
        action_expand(j, v[j + num_in]);
      }
    });
  }
}

void SpiralServer::FoldCiphertexts(
    std::vector<PolyMatrixRaw>& v_cts,
    const std::vector<PolyMatrixNtt>& v_folding,
    const std::vector<PolyMatrixNtt>& v_folding_neg) const {
  if (v_cts.size() == 1) {
    return;
  }
  size_t further_dims = arith::Log2(v_cts.size());
  size_t ell = v_folding[0].Cols() >> 1;

  size_t num_per = v_cts.size();
  for (size_t cur_dim = 0; cur_dim < further_dims; ++cur_dim) {
    num_per = num_per / 2;

    // a big improve for performance
    yacl::parallel_for(0, num_per, [&](size_t begin, size_t end) {
      for (size_t i = begin; i < end; ++i) {
        auto g_inv_c = PolyMatrixRaw::Zero(params_.PolyLen(), 2 * ell, 1);
        auto g_inv_c_ntt = PolyMatrixNtt::Zero(params_.CrtCount(),
                                               params_.PolyLen(), 2 * ell, 1);
        auto prod =
            PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);
        auto sum =
            PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);

        util::GadgetInvert(params_, g_inv_c, v_cts[i]);
        ToNtt(params_, g_inv_c_ntt, g_inv_c);
        Multiply(params_, prod, v_folding_neg[further_dims - 1 - cur_dim],
                 g_inv_c_ntt);

        util::GadgetInvert(params_, g_inv_c, v_cts[num_per + i]);
        ToNtt(params_, g_inv_c_ntt, g_inv_c);
        Multiply(params_, sum, v_folding[further_dims - 1 - cur_dim],
                 g_inv_c_ntt);
        AddInto(params_, sum, prod);
        FromNtt(params_, v_cts[i], sum);
      }
    });
  }
}

std::vector<PolyMatrixNtt> SpiralServer::GetVFoldingNeg(
    std::vector<PolyMatrixNtt>& v_folding) const {
  auto gadget = util::BuildGadget(params_, 2, 2 * params_.TGsw());
  auto gadget_ntt = ToNtt(params_, gadget);

  std::vector<PolyMatrixNtt> v_folding_neg;
  v_folding_neg.reserve(params_.DbDim2());

  for (size_t i = 0; i < params_.DbDim2(); ++i) {
    // -C
    auto ct_gsw_inv =
        PolyMatrixRaw::Zero(params_.PolyLen(), 2, 2 * params_.TGsw());
    Invert(params_, ct_gsw_inv, FromNtt(params_, v_folding[i]));
    // G_{n+1, z} - C
    auto ct_gsw_neg = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(),
                                          2, 2 * params_.TGsw());
    Add(params_, ct_gsw_neg, gadget_ntt, ToNtt(params_, ct_gsw_inv));
    v_folding_neg.push_back(std::move(ct_gsw_neg));
  }
  return v_folding_neg;
}

PolyMatrixNtt SpiralServer::Pack(const std::vector<PolyMatrixRaw>& v_ct,
                                 const std::vector<PolyMatrixNtt>& v_w) const {
  WEAK_ENFORCE(v_ct.size() >= params_.N() * params_.N());
  WEAK_ENFORCE(v_w.size() == params_.N());
  WEAK_ENFORCE(v_ct[0].Rows() == static_cast<size_t>(2));
  WEAK_ENFORCE(v_ct[0].Cols() == static_cast<size_t>(1));
  WEAK_ENFORCE(v_w[0].Rows() == static_cast<size_t>(params_.N() + 1));
  WEAK_ENFORCE(v_w[0].Cols() == static_cast<size_t>(params_.TConv()));

  auto result = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(),
                                    params_.N() + 1, params_.N());
  auto g_inv = PolyMatrixRaw::Zero(params_.PolyLen(), params_.TConv(), 1);
  auto g_inv_ntt = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(),
                                       params_.TConv(), 1);

  auto prod = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(),
                                  params_.N() + 1, 1);

  auto ct1 = PolyMatrixRaw::Zero(params_.PolyLen(), 1, 1);
  auto ct2 = PolyMatrixRaw::Zero(params_.PolyLen(), 1, 1);
  auto ct2_ntt =
      PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 1, 1);

  for (size_t j = 0; j < params_.N(); ++j) {
    // each one row
    auto v_int = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(),
                                     params_.N() + 1, 1);

    for (size_t i = 0; i < params_.N(); ++i) {
      const auto& w = v_w[i];
      const auto& ct = v_ct[i * params_.N() + j];
      // copy to ct1
      std::memcpy(ct1.Data().data(), ct.Data().data(),
                  sizeof(uint64_t) * ct.NumWords());
      // copy to ct2
      std::memcpy(ct2.Data().data(), ct.Data().data() + ct.PolyStartIndex(1, 0),
                  sizeof(uint64_t) * ct.NumWords());
      // ntt
      ToNtt(params_, ct2_ntt, ct2);
      util::GadgetInvert(params_, g_inv, ct1);
      ToNtt(params_, g_inv_ntt, g_inv);
      Multiply(params_, prod, w, g_inv_ntt);
      AddIntoAt(params_, v_int, ct2_ntt, i + 1, 0);
      AddInto(params_, v_int, prod);
    }
    result.CopyInto(v_int, 0, j);
  }

  return result;
}

std::vector<PolyMatrixRaw> SpiralServer::ProcessQuery(
    const SpiralQuery& query) const {
  YACL_ENFORCE(database_seted_,
               "Before ProcessQuery, database must be processed");

  size_t dim0 = 1 << params_.DbDim1();
  size_t num_per = 1 << params_.DbDim2();
  size_t db_slice_sz = dim0 * num_per * params_.PolyLen();

  const auto& v_packing = pks_.v_packing_;

  std::vector<uint64_t> v_reg_reoriented;
  std::vector<PolyMatrixNtt> v_folding;

  SPDLOG_INFO("Server begin to Expand Query");
  yacl::ElapsedTimer timer;

  // We default to using QueryExpand technology
  std::tie(v_reg_reoriented, v_folding) = ExpandQuery(query);

  SPDLOG_INFO("Server end to Expand Query, time cost: {} ms", timer.CountMs());
  timer.Restart();

  auto v_folding_neg = GetVFoldingNeg(v_folding);
  size_t n_power = params_.N() * params_.N();
  std::vector<PolyMatrixRaw> v_packed_ct;
  v_packed_ct.reserve(partition_num_);

  // init only once
  std::vector<PolyMatrixNtt> intermediate;
  std::vector<PolyMatrixRaw> intermediate_raw;
  for (size_t i = 0; i < num_per; ++i) {
    intermediate.emplace_back(params_.CrtCount(), params_.PolyLen(), 2, 1);
    intermediate_raw.emplace_back(params_.PolyLen(), 2, 1);
  }

  // when use yacl::parallel_for, there is no improve
  for (size_t partiton_idx = 0; partiton_idx < partition_num_; ++partiton_idx) {
    std::vector<PolyMatrixRaw> v_ct;
    for (size_t trial = 0; trial < n_power; ++trial) {
      // the instances is 1, so the ins = 0
      // so we can remove the ins
      size_t idx = trial * db_slice_sz;
      MultiplyRegByDatabase(intermediate, v_reg_reoriented, dim0, num_per, idx,
                            partiton_idx);
      // ntt to raw
      for (size_t i = 0; i < intermediate.size(); ++i) {
        FromNtt(params_, intermediate_raw[i], intermediate[i]);
      }
      // fold
      FoldCiphertexts(intermediate_raw, v_folding, v_folding_neg);
      // need deep-copy
      v_ct.emplace_back(intermediate_raw[0]);
    }
    auto packed_ct = Pack(v_ct, v_packing);
    v_packed_ct.push_back(FromNtt(params_, packed_ct));
  }

  // modulus switching
  uint64_t q1 = 4 * params_.PtModulus();
  uint64_t q2 = kQ2Values[params_.Q2Bits()];
  for (auto& ct : v_packed_ct) {
    ct.Rescale(0, 1, params_.Modulus(), q2);
    ct.Rescale(1, ct.Rows(), params_.Modulus(), q1);
  }

  SPDLOG_INFO(
      "Server end to do dot-product between query and database, time cost: {} "
      "ms",
      timer.CountMs());

  return v_packed_ct;
}

void SpiralServer::RegevToGsw(std::vector<PolyMatrixNtt>& v_gsw,
                              const std::vector<PolyMatrixNtt>& v_inp,
                              const PolyMatrixNtt& v, size_t idx_factor,
                              size_t idx_offset) const {
  WEAK_ENFORCE(v.Rows() == static_cast<size_t>(2));
  WEAK_ENFORCE(v.Cols() == static_cast<size_t>(2 * params_.TConv()));

  // a big improve for performance
  yacl::parallel_for(0, v_gsw.size(), [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      auto& ct = v_gsw[i];

      auto ginv_c_inp =
          PolyMatrixRaw::Zero(params_.PolyLen(), 2 * params_.TConv(), 1);
      auto ginv_c_inp_ntt = PolyMatrixNtt::Zero(
          params_.CrtCount(), params_.PolyLen(), 2 * params_.TConv(), 1);

      auto tmp_ct_raw = PolyMatrixRaw::Zero(params_.PolyLen(), 2, 1);
      auto tmp_ct_ntt =
          PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);

      for (size_t j = 0; j < params_.TGsw(); ++j) {
        size_t idx_ct = i * params_.TGsw() + j;
        size_t idx_inp = idx_factor * idx_ct + idx_offset;

        ct.CopyInto(v_inp[idx_inp], 0, 2 * j + 1);
        FromNtt(params_, tmp_ct_raw, v_inp[idx_inp]);
        util::GadgetInvert(params_, ginv_c_inp, tmp_ct_raw);

        ToNtt(params_, ginv_c_inp_ntt, ginv_c_inp);
        Multiply(params_, tmp_ct_ntt, v, ginv_c_inp_ntt);
        ct.CopyInto(tmp_ct_ntt, 0, 2 * j);
      }
    }
  });
}

std::pair<std::vector<uint64_t>, std::vector<PolyMatrixNtt>>
SpiralServer::ExpandQuery(const SpiralQuery& query) const {
  size_t dim0 = 1 << params_.DbDim1();
  size_t further_dims = params_.DbDim2();
  size_t num_bits_to_gen = params_.TGsw() * further_dims + dim0;
  size_t g = arith::Log2Ceil(num_bits_to_gen);

  size_t right_expanded = params_.TGsw() * further_dims;
  size_t stop_round = arith::Log2Ceil(right_expanded);

  std::vector<PolyMatrixNtt> v;
  v.reserve(static_cast<size_t>(1) << g);
  for (size_t i = 0; i < static_cast<size_t>(1) << g; ++i) {
    v.emplace_back(params_.CrtCount(), params_.PolyLen(), 2, 1);
  }

  auto query_ct_ntt = ToNtt(params_, query.ct_);
  v[0].CopyInto(query_ct_ntt, 0, 0);

  const PolyMatrixNtt& v_conversion = pks_.v_conversion_[0];
  const std::vector<PolyMatrixNtt>& v_w_left = pks_.v_expansion_left_;
  const std::vector<PolyMatrixNtt>& v_w_right = pks_.v_expansion_right_;

  auto v_neg1 = GetVneg1(params_);

  std::vector<PolyMatrixNtt> v_reg_inp;
  v_reg_inp.reserve(dim0);
  std::vector<PolyMatrixNtt> v_gsw_inp;
  v_gsw_inp.reserve(right_expanded);

  yacl::ElapsedTimer timer;

  if (further_dims > 0) {
    CoefficientExpansion(v, g, stop_round, v_w_left, v_w_right, v_neg1,
                         params_.TGsw() * params_.DbDim2());

    for (size_t i = 0; i < dim0; ++i) {
      // deep copy
      v_reg_inp.push_back(std::move(v[2 * i]));
    }
    for (size_t i = 0; i < right_expanded; ++i) {
      v_gsw_inp.push_back(std::move(v[2 * i + 1]));
    }
  } else {
    CoefficientExpansion(v, g, 0, v_w_left, v_w_right, v_neg1, 0);
    for (size_t i = 0; i < dim0; ++i) {
      v_reg_inp.emplace_back(v[i]);
    }
  }

  SPDLOG_INFO("Server finished CoefficientExpansion, time: {} ms",
              timer.CountMs());
  timer.Restart();

  size_t v_reg_sz = dim0 * 2 * params_.PolyLen();
  std::vector<uint64_t> v_reg_reoriented(v_reg_sz);
  ReorientRegCiphertexts(params_, v_reg_reoriented, v_reg_inp);

  SPDLOG_INFO("Server finished ReorientRegCiphertexts, time: {} ms",
              timer.CountMs());
  timer.Restart();

  std::vector<PolyMatrixNtt> v_folding;
  for (size_t i = 0; i < params_.DbDim2(); ++i) {
    v_folding.emplace_back(params_.CrtCount(), params_.PolyLen(), 2,
                           2 * params_.TGsw());
  }
  RegevToGsw(v_folding, v_gsw_inp, v_conversion, 1, 0);
  SPDLOG_INFO("Server finished RegevToGsw, time: {} ms", timer.CountMs());

  return std::make_pair(v_reg_reoriented, v_folding);
}

void SpiralServer::MultiplyRegByDatabase(std::vector<PolyMatrixNtt>& out,
                                         std::vector<uint64_t>& v_first_dim,
                                         size_t dim0, size_t num_per,
                                         size_t cur_db_idx,
                                         size_t partiiton_idx) const {
  size_t ct_rows = 2;
  size_t ct_cols = 1;
  size_t pt_rows = 1;
  size_t pt_cols = 1;

  yacl::parallel_for(0, params_.PolyLen(), [&](size_t begin, size_t end) {
    for (size_t z = begin; z < end; ++z) {
      size_t idx_a_base = z * (ct_cols * dim0 * ct_rows);
      size_t idx_b_base = z * (num_per * pt_cols * dim0 * pt_rows);

      for (size_t i = 0; i < num_per; ++i) {
        for (size_t c = 0; c < pt_cols; ++c) {
          uint128_t sums_out_n0_0 = 0;
          uint128_t sums_out_n0_1 = 0;
          uint128_t sums_out_n1_0 = 0;
          uint128_t sums_out_n1_1 = 0;

          for (size_t jm = 0; jm < dim0 * pt_rows; ++jm) {
            uint64_t b = reoriented_dbs_[partiiton_idx * single_db_size_ +
                                         cur_db_idx + idx_b_base];
            idx_b_base += 1;

            uint64_t v_a0 = v_first_dim[idx_a_base + jm * ct_rows];
            uint64_t v_a1 = v_first_dim[idx_a_base + jm * ct_rows + 1];

            uint64_t b_lo = b & 0x00000000FFFFFFFFULL;
            uint64_t b_hi = b >> 32;

            uint64_t v_a0_lo = v_a0 & 0x00000000FFFFFFFFULL;
            uint64_t v_a0_hi = v_a0 >> 32;

            uint64_t v_a1_lo = v_a1 & 0x00000000FFFFFFFFULL;
            uint64_t v_a1_hi = v_a1 >> 32;
            // d0 n0
            sums_out_n0_0 += (static_cast<uint128_t>(v_a0_lo * b_lo));
            sums_out_n0_1 += (static_cast<uint128_t>(v_a1_lo * b_lo));
            // do n1
            sums_out_n1_0 += (static_cast<uint128_t>(v_a0_hi * b_hi));
            sums_out_n1_1 += (static_cast<uint128_t>(v_a1_hi * b_hi));
          }
          // output n0
          size_t crt_count = params_.CrtCount();
          size_t poly_len = params_.PolyLen();

          size_t n = 0;
          size_t idx_c = c * (crt_count * poly_len) + n * poly_len + z;
          out[i].Data()[idx_c] = arith::BarrettReductionU128Raw(
              sums_out_n0_0, params_.BarrettCr0(0), params_.BarrettCr1(0),
              params_.Moduli(0));
          // update idx
          idx_c += (pt_cols * crt_count * poly_len);
          out[i].Data()[idx_c] = arith::BarrettReductionU128Raw(
              sums_out_n0_1, params_.BarrettCr0(0), params_.BarrettCr1(0),
              params_.Moduli(0));
          // output n1
          n = 1;
          // reset idx
          idx_c = c * (crt_count * poly_len) + n * poly_len + z;
          out[i].Data()[idx_c] = arith::BarrettReductionU128Raw(
              sums_out_n1_0, params_.BarrettCr0(1), params_.BarrettCr1(1),
              params_.Moduli(1));
          idx_c += (pt_cols * crt_count * poly_len);
          out[i].Data()[idx_c] = arith::BarrettReductionU128Raw(
              sums_out_n1_1, params_.BarrettCr0(1), params_.BarrettCr1(1),
              params_.Moduli(1));
        }
      }
    }
  });
}

// util methods
std::vector<PolyMatrixNtt> GetVneg1(const Params& params) {
  std::vector<PolyMatrixNtt> v_neg1;
  v_neg1.reserve(params.PolyLenLog2());
  for (size_t j = 0; j < params.PolyLenLog2(); ++j) {
    auto idx = params.PolyLen() - (static_cast<size_t>(1) << j);
    auto ng1 = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
    ng1.Data()[idx] = 1ULL;
    v_neg1.push_back(ToNtt(params, Invert(params, ng1)));
  }
  return v_neg1;
}

std::pair<PolyMatrixRaw, std::vector<uint64_t>> GenRandomDbAndGetItem(
    const Params& params, size_t item_idx) {
  yacl::crypto::Prg<uint64_t> rng(yacl::crypto::SecureRandU128());
  size_t instances = params.Instances();
  size_t trials = params.N() * params.N();
  size_t dim0 = 1 << params.DbDim1();
  size_t num_per = 1 << params.DbDim2();
  size_t num_items = dim0 * num_per;

  size_t db_size_words = instances * trials * num_items * params.PolyLen();
  // a large matrix, the right item
  auto item =
      PolyMatrixRaw::Zero(params.PolyLen(), instances * params.N(), params.N());
  std::vector<uint64_t> v(db_size_words);

  for (size_t ins = 0; ins < instances; ++ins) {
    for (size_t trial = 0; trial < trials; ++trial) {
      for (size_t i = 0; i < num_items; ++i) {
        size_t ii = i % num_per;
        size_t j = i / num_per;
        // a random item
        auto db_item = PolyMatrixRaw::RandomPrg(params, 1, 1, rng);
        db_item.ReduceMod(params.PtModulus());
        if (i == item_idx) {
          item.CopyInto(db_item, ins * params.N() + trial / params.N(),
                        trial % params.N());
        }

        for (size_t z = 0; z < params.PolyLen(); ++z) {
          db_item.Data()[z] = arith::RecenertMod(
              db_item.Data()[z], params.PtModulus(), params.Modulus());
        }
        // auto db_item_ntt = db_item.Ntt();
        auto db_item_ntt = ToNtt(params, db_item);
        for (size_t z = 0; z < params.PolyLen(); ++z) {
          size_t idx_dst = util::CalcIndex(
              {ins, trial, z, ii, j},
              {instances, trials, params.PolyLen(), num_per, dim0});
          v[idx_dst] =
              db_item_ntt.Data()[z] |
              (db_item_ntt.Data()[params.PolyLen() + z] << kPackedOffset2);
        }
      }
    }
  }

  return std::make_pair(item, v);
}

std::vector<uint64_t> ReorientDatabase(
    const Params& params, std::vector<std::vector<uint64_t>>& database) {
  size_t instances = params.Instances();
  YACL_ENFORCE_EQ(instances, static_cast<size_t>(1));

  size_t trials = params.N() * params.N();
  size_t dim0 = 1 << params.DbDim1();
  size_t num_per = 1 << params.DbDim2();
  size_t num_items = dim0 * num_per;

  size_t db_size_words = instances * trials * num_items * params.PolyLen();

  std::vector<uint64_t> v(db_size_words);

  for (size_t ins = 0; ins < instances; ++ins) {
    yacl::parallel_for(0, trials, [&](size_t begin, size_t end) {
      for (size_t trial = begin; trial < end; ++trial) {
        for (size_t i = 0; i < num_items; ++i) {
          size_t ii = i % num_per;
          size_t j = i / num_per;

          auto db_item = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
          size_t start_idx = trial * params.PolyLen();
          std::memcpy(db_item.Data().data(), database[i].data() + start_idx,
                      params.PolyLen() * sizeof(uint64_t));

          for (size_t z = 0; z < params.PolyLen(); ++z) {
            db_item.Data()[z] = arith::RecenertMod(
                db_item.Data()[z], params.PtModulus(), params.Modulus());
          }
          // auto db_item_ntt = db_item.Ntt();
          auto db_item_ntt = ToNtt(params, db_item);
          for (size_t z = 0; z < params.PolyLen(); ++z) {
            size_t idx_dst = util::CalcIndex(
                {ins, trial, z, ii, j},
                {instances, trials, params.PolyLen(), num_per, dim0});
            v[idx_dst] =
                db_item_ntt.Data()[z] |
                (db_item_ntt.Data()[params.PolyLen() + z] << kPackedOffset2);
          }
        }
      }
    });
  }
  return v;
}

}  // namespace psi::spiral