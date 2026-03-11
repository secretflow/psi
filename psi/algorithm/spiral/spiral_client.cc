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

#include "psi/algorithm/spiral/spiral_client.h"

#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/utils/elapsed_timer.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/spiral/arith/arith.h"
#include "psi/algorithm/spiral/arith/ntt.h"
#include "psi/algorithm/spiral/common.h"
#include "psi/algorithm/spiral/gadget.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

yacl::Buffer SpiralQuery::Serialize() {
  SpiralQueryProto proto;
  // only one possible
  *proto.mutable_ct() = ct_.ToProto();
  proto.set_seed(reinterpret_cast<const uint8_t*>(&seed_), sizeof(uint128_t));

  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}
std::string SpiralQuery::SerializeToStr() {
  SpiralQueryProto proto;
  // only one possible
  *proto.mutable_ct() = ct_.ToProto();
  proto.set_seed(reinterpret_cast<const uint8_t*>(&seed_), sizeof(uint128_t));

  return proto.SerializeAsString();
}

SpiralQuery SpiralQuery::Deserialize(const Params& params,
                                     const yacl::ByteContainerView& buffer) {
  SpiralQueryProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  SpiralQuery query;
  query.ct_ = PolyMatrixRaw::FromProto(proto.ct(), params);

  uint128_t seed{0};
  std::memcpy(&seed, proto.seed().data(), sizeof(uint128_t));
  query.seed_ = seed;

  return query;
}

yacl::Buffer SpiralQuery::SerializeRng() {
  SpiralQueryProto proto;
  *proto.mutable_ct() = ct_.ToProtoRng();

  proto.set_seed(reinterpret_cast<const uint8_t*>(&seed_), sizeof(uint128_t));

  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}
std::string SpiralQuery::SerializeRngToStr() {
  SpiralQueryProto proto;
  *proto.mutable_ct() = ct_.ToProtoRng();

  proto.set_seed(reinterpret_cast<const uint8_t*>(&seed_), sizeof(uint128_t));

  return proto.SerializeAsString();
}

SpiralQuery SpiralQuery::DeserializeRng(const Params& params,
                                        const yacl::ByteContainerView& buffer) {
  SpiralQueryProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  // first we construct seed and prg
  uint128_t seed{0};
  std::memcpy(&seed, proto.seed().data(), sizeof(uint128_t));

  yacl::crypto::Prg<uint64_t> rng(seed);

  SpiralQuery query;
  query.seed_ = seed;

  query.ct_ = PolyMatrixRaw::FromProtoRng(proto.ct(), params, rng);

  return query;
}

void SpiralClient::Init() {
  // first init q2_params_
  q2_params_ = Params::ParamsWithModuli(params_, {kQ2Values[params_.Q2Bits()]});

  auto [sk_gsw_rows, sk_gsw_cols] = params_.GetSkGswDims();
  auto [sk_regev_rows, sk_regev_cols] = params_.GetSkRegDims();

  sk_gsw_ = PolyMatrixRaw::Zero(params_.PolyLen(), sk_gsw_rows, sk_gsw_cols);

  sk_reg_ =
      PolyMatrixRaw::Zero(params_.PolyLen(), sk_regev_rows, sk_regev_cols);

  dg_ = DiscreteGaussian(params_.NoiseWidth());
  // Note: Secret keys are NOT generated here anymore.
  // Constructors must call GenSecretKeys() explicitly.
}

PolyMatrixRaw SpiralClient::GetFreshGswPublicKey(
    size_t m, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  size_t n = params_.N();

  auto a = PolyMatrixRaw::RandomPrg(params_, 1, m, rng_pub);
  auto a_ntt = ToNtt(params_, a);
  auto e = Noise(params_, n, m, dg_, rng);
  auto e_ntt = ToNtt(params_, e);
  auto a_inv = Negate(params_, a);
  auto sk_gsw_ntt = ToNtt(params_, sk_gsw_);
  auto b_p = Multiply(params_, sk_gsw_ntt, a_ntt);
  auto b = Add(params_, e_ntt, b_p);
  auto b_raw = FromNtt(params_, b);

  return Stack(a_inv, b_raw);
}

PolyMatrixNtt SpiralClient::GetRegevSample(
    yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  auto a = PolyMatrixRaw::RandomPrg(params_, 1, 1, rng_pub);
  auto a_ntt = ToNtt(params_, a);
  auto a_inv = ToNtt(params_, Negate(params_, a));
  auto e = Noise(params_, 1, 1, dg_, rng);

  auto e_ntt = ToNtt(params_, e);
  auto sk_reg_ntt = ToNtt(params_, sk_reg_);
  auto b_p = Multiply(params_, sk_reg_ntt, a_ntt);
  auto b = Add(params_, e_ntt, b_p);
  auto p = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);
  p.CopyInto(a_inv, 0, 0);
  p.CopyInto(b, 1, 0);

  return p;
}

PolyMatrixNtt SpiralClient::GetScaledRegevSample(
    yacl::crypto::Prg<uint64_t>& rng, yacl::crypto::Prg<uint64_t>& rng_pub,
    uint64_t scale) const {
  auto a = PolyMatrixRaw::RandomPrg(params_, 1, 1, rng_pub);
  auto a_ntt = ToNtt(params_, a);
  auto a_inv = ToNtt(params_, Negate(params_, a));
  auto e = Noise(params_, 1, 1, dg_, rng);

  for (size_t i = 0; i < params_.PolyLen(); ++i) {
    e.Data()[i] = arith::MultiplyUintMod(e.Data()[i], scale, params_.Modulus());
  }

  auto e_ntt = ToNtt(params_, e);
  auto sk_reg_ntt = ToNtt(params_, sk_reg_);
  auto b_p = Multiply(params_, sk_reg_ntt, a_ntt);
  auto b = Add(params_, e_ntt, b_p);

  auto p = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, 1);
  p.CopyInto(a_inv, 0, 0);
  p.CopyInto(b, 1, 0);

  return p;
}

PolyMatrixNtt SpiralClient::GetFreshRegevPublicKey(
    size_t m, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  auto p = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, m);
  for (size_t i = 0; i < m; ++i) {
    p.CopyInto(GetRegevSample(rng, rng_pub), 0, i);
  }
  return p;
}

PolyMatrixNtt SpiralClient::GetFreshScaledRegevPublicKey(
    size_t m, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub, uint64_t scale) const {
  auto p = PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), 2, m);
  for (size_t i = 0; i < m; ++i) {
    p.CopyInto(GetScaledRegevSample(rng, rng_pub, scale), 0, i);
  }
  return p;
}

PolyMatrixNtt SpiralClient::EncryptMatrixGsw(
    PolyMatrixNtt& ag, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  YACL_ENFORCE(sk_inited_, "Secret Key must be inited");
  auto mx = ag.Cols();
  auto p = GetFreshGswPublicKey(mx, rng, rng_pub);
  auto p_ntt = ToNtt(params_, p);

  auto res = Add(params_, p_ntt, ag.PadTop(1));
  return res;
}

PolyMatrixNtt SpiralClient::EncryptMatrixRegev(
    PolyMatrixNtt& a, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  YACL_ENFORCE(sk_inited_, "Secret Key must be inited");
  auto m = a.Cols();
  auto p = GetFreshRegevPublicKey(m, rng, rng_pub);
  return Add(params_, p, a.PadTop(1));
}

PolyMatrixNtt SpiralClient::EncryptMatrixScaledRegev(
    PolyMatrixNtt& a, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub, uint64_t scale) const {
  YACL_ENFORCE(sk_inited_, "Secret Key must be inited");
  auto m = a.Cols();
  auto p = GetFreshScaledRegevPublicKey(m, rng, rng_pub, scale);
  return Add(params_, p, a.PadTop(1));
}

void SpiralClient::GenSecretKeys(yacl::crypto::Prg<uint64_t>& rng) {
  GenTernaryMatrix(params_, sk_gsw_, kHammingWeight, rng);
  GenTernaryMatrix(params_, sk_reg_, kHammingWeight, rng);
  sk_gsw_full_ = MatrixWithIdentity(sk_gsw_);
  sk_reg_full_ = MatrixWithIdentity(sk_reg_);
  sk_inited_ = true;
}

std::vector<PolyMatrixNtt> SpiralClient::GenExpansionParams(
    size_t num_exp, size_t m_exp, yacl::crypto::Prg<uint64_t>& rng,
    yacl::crypto::Prg<uint64_t>& rng_pub) const {
  auto g_exp = util::BuildGadget(params_, 1, m_exp);
  auto g_exp_ntt = ToNtt(params_, g_exp);

  std::vector<PolyMatrixNtt> res;
  for (size_t i = 0; i < num_exp; ++i) {
    size_t t = (params_.PolyLen() / (1 << i)) + 1;
    auto tau_sk_reg = Automorphism(params_, sk_reg_, t);
    auto tau_sk_reg_ntt = ToNtt(params_, tau_sk_reg);
    auto prod = Multiply(params_, tau_sk_reg_ntt, g_exp_ntt);
    auto w_exp_i = EncryptMatrixRegev(prod, rng, rng_pub);
    res.push_back(w_exp_i);
  }
  return res;
}

PublicKeys SpiralClient::GenPublicKeys() const {
  YACL_ENFORCE(sk_inited_,
               "Before GenPublicKeys, the secret key must be inited.");

  auto sk_reg_ntt = ToNtt(params_, sk_reg_);
  auto sk_gsw_ntt = ToNtt(params_, sk_gsw_);

  PublicKeys pp;
  uint128_t seed = yacl::MakeUint128(0, 10001);
  yacl::crypto::Prg<uint64_t> rng(seed);
  uint128_t pub_seed = yacl::MakeUint128(0, 10002);
  yacl::crypto::Prg<uint64_t> rng_pub(pub_seed);

  // params for packing
  size_t t_conv = params_.TConv();
  auto gadget_conv = util::BuildGadget(params_, 1, t_conv);
  auto gadget_conv_ntt = ToNtt(params_, gadget_conv);

  size_t n = params_.N();
  // only consider the version = 0
  size_t num_packing_mats = n;

  pp.v_packing_.reserve(num_packing_mats);
  auto scaled = ScalarMultiply(params_, sk_reg_ntt, gadget_conv_ntt);
  for (size_t i = 0; i < num_packing_mats; ++i) {
    // Page-20, Packing Regev encodings, PackSetup
    auto ag =
        PolyMatrixNtt::Zero(params_.CrtCount(), params_.PolyLen(), n, t_conv);
    ag.CopyInto(scaled, i, 0);
    auto w = EncryptMatrixGsw(ag, rng, rng_pub);
    pp.v_packing_.push_back(w);
  }

  // we defaulta use QueryExpand = true
  pp.v_expansion_left_ =
      GenExpansionParams(params_.G(), params_.TExpLeft(), rng, rng_pub);
  // we only using the version = 0, so pp.v_expansion_right_ must be inited
  pp.v_expansion_right_ = GenExpansionParams(params_.StopRound() + 1,
                                             params_.TExpRight(), rng, rng_pub);
  // params for conversion
  auto g_conv = util::BuildGadget(params_, 2, 2 * t_conv);
  auto sk_reg_square_ntt = Multiply(params_, sk_reg_ntt, sk_reg_ntt);
  // Page-14 (3.1)
  pp.v_conversion_ = std::vector<PolyMatrixNtt>{PolyMatrixNtt::Zero(
      params_.CrtCount(), params_.PolyLen(), 2, 2 * t_conv)};

  for (size_t i = 0; i < 2 * t_conv; ++i) {
    PolyMatrixNtt sigma;
    if (i % 2 == 0) {
      uint64_t val = g_conv.Poly(0, i)[0];
      auto val_ntt =
          ToNtt(params_, PolyMatrixRaw::SingleValue(params_.PolyLen(), val));
      sigma = Multiply(params_, sk_reg_square_ntt, val_ntt);
    } else {
      uint64_t val = g_conv.Poly(1, i)[0];
      auto val_ntt =
          ToNtt(params_, PolyMatrixRaw::SingleValue(params_.PolyLen(), val));
      sigma = Multiply(params_, sk_reg_ntt, val_ntt);
    }
    auto ct = EncryptMatrixRegev(sigma, rng, rng_pub);
    pp.v_conversion_[0].CopyInto(ct, 0, i);
  }

  return pp;
}

SpiralQuery SpiralClient::GenQueryInternal(size_t pt_idx_target) const {
  size_t further_dims = params_.DbDim2();
  size_t idx_dim0 = pt_idx_target / (1 << further_dims);
  size_t idx_futher = pt_idx_target % (1 << further_dims);
  uint64_t scale_k = params_.ScaleK();
  size_t t_gsw = params_.TGsw();
  size_t bits_per = util::GetBitsPer(params_, t_gsw);

  uint64_t modulus_cr0 = params_.BarrettCr0Modulus();
  uint64_t modulus_cr1 = params_.BarrettCr1Modulus();

  yacl::crypto::Prg<uint64_t> rng(yacl::MakeUint128(0, 10003));
  // a empty query
  SpiralQuery query;
  uint128_t query_seed = yacl::MakeUint128(0, 10004);
  yacl::crypto::Prg<uint64_t> rng_pub(query_seed);
  query.seed_ = query_seed;

  // We default to using QueryExpand technology
  auto sigma = PolyMatrixRaw::Zero(params_.PolyLen(), 1, 1);
  uint64_t inv_2_g_first =
      arith::InvertUintMod(1 << params_.G(), params_.ModulusSeal());
  uint64_t inv_2_g_rest = arith::InvertUintMod(1 << (params_.StopRound() + 1),
                                               params_.ModulusSeal());

  if (params_.DbDim2() == 0) {
    for (size_t i = 0; i < static_cast<size_t>(1 << params_.DbDim1()); ++i) {
      if (i == idx_dim0) {
        sigma.Data()[i] = scale_k;
      }
    }
    for (size_t i = 0; i < params_.PolyLen(); ++i) {
      sigma.Data()[i] =
          arith::MultiplyUintMod(sigma.Data()[i], inv_2_g_first,
                                 params_.Modulus(), modulus_cr0, modulus_cr1);
    }

  } else {
    for (size_t i = 0; i < static_cast<size_t>(1 << params_.DbDim1()); ++i) {
      if (i == idx_dim0) {
        sigma.Data()[2 * i] = scale_k;
      }
    }

    for (size_t l = 0; l < further_dims; ++l) {
      uint64_t mask = 1ULL << l;
      bool bit = (idx_futher & mask) == mask;
      for (size_t k = 0; k < params_.TGsw(); ++k) {
        uint64_t val = bit ? 1ULL << (bits_per * k) : 0;
        size_t idx = l * params_.TGsw() + k;
        sigma.Data()[2 * idx + 1] = val;
      }
    }

    for (size_t i = 0; i < (params_.PolyLen() >> 1); ++i) {
      sigma.Data()[2 * i] =
          arith::MultiplyUintMod(sigma.Data()[2 * i], inv_2_g_first,
                                 params_.Modulus(), modulus_cr0, modulus_cr1);

      sigma.Data()[2 * i + 1] =
          arith::MultiplyUintMod(sigma.Data()[2 * i + 1], inv_2_g_rest,
                                 params_.Modulus(), modulus_cr0, modulus_cr1);
    }
  }
  auto sigma_ntt = ToNtt(params_, sigma);
  // when use ExpandQuery, the Query contains only one Regev Ciphertext
  query.ct_ = FromNtt(params_, EncryptMatrixRegev(sigma_ntt, rng, rng_pub));

  return query;
}

std::vector<uint8_t> SpiralClient::DecodeResponse(
    const std::vector<PolyMatrixRaw>& response, size_t raw_idx_target) const {
  YACL_ENFORCE_EQ(response.size(), partition_num_);

  yacl::ElapsedTimer timer;

  std::vector<std::vector<uint8_t>> results;
  results.reserve(partition_num_);
  for (size_t i = 0; i < partition_num_; ++i) {
    auto decode_result = DecodeResponseInternal(response[i]);

    // now we extract the data from our target row
    YACL_ENFORCE_EQ(decode_result.Data().size(), params_.PtCoeffs());

    auto bytes = util::ConvertCoeffsToBytes(decode_result.Data(),
                                            params_.PtModulusBitLen());
    size_t offset = raw_idx_target % element_size_of_pt_;

    // note that the computation of element_byte_len
    size_t element_byte_len =
        std::min(params_.MaxByteLenOfPt(), database_info_.byte_size_per_row_);
    std::vector<uint8_t> tmp(element_byte_len);
    // just copy
    std::memcpy(tmp.data(), bytes.data() + offset * element_byte_len,
                sizeof(uint8_t) * element_byte_len);
    // save
    results.push_back(std::move(tmp));
  }

  auto final_result = psi::pir::RawDatabase::Combine(
      results, database_info_.byte_size_per_row_);
  return final_result;
}

PolyMatrixRaw SpiralClient::DecodeResponseInternal(
    const PolyMatrixRaw& response) const {
  uint64_t p = params_.PtModulus();
  uint64_t q1 = 4 * p;
  uint64_t q2 = kQ2Values[params_.Q2Bits()];
  size_t n = params_.N();
  size_t poly_len = params_.PolyLen();

  auto sk_gsw_q2 = PolyMatrixRaw::Zero(q2_params_.PolyLen(), n, 1);
  for (size_t i = 0; i < poly_len * n; ++i) {
    sk_gsw_q2.Data()[i] =
        arith::Recenter(sk_gsw_.Data()[i], params_.Modulus(), q2);
  }
  // for mul
  auto sk_gsw_q2_ntt =
      PolyMatrixNtt::Zero(q2_params_.CrtCount(), q2_params_.PolyLen(), n, 1);
  ToNtt(q2_params_, sk_gsw_q2_ntt, sk_gsw_q2);

  auto result = PolyMatrixRaw::Zero(params_.PolyLen(), n, n);

  auto packed_ct = response;
  // \hat c_1
  auto first_row = packed_ct.SubMatrix(0, 0, 1, packed_ct.Cols());
  // \hat c_2
  auto rest_rows =
      packed_ct.SubMatrix(1, 0, packed_ct.Rows() - 1, packed_ct.Cols());
  // (1, n)
  auto first_row_q2 =
      PolyMatrixNtt::Zero(q2_params_.CrtCount(), q2_params_.PolyLen(), 1, n);
  ToNtt(q2_params_, first_row_q2, first_row);
  // \tilde s \hat c_1^T
  auto sk_prod =
      FromNtt(q2_params_, Multiply(q2_params_, sk_gsw_q2_ntt, first_row_q2));

  auto q1_i64 = static_cast<int64_t>(q1);
  auto q2_i64 = static_cast<int64_t>(q2);
  int128_t p_i128 = yacl::MakeInt128(0, p);

  for (size_t i = 0; i < n * n * poly_len; ++i) {
    WEAK_ENFORCE(i < sk_prod.Data().size());
    auto val_first = static_cast<int64_t>(sk_prod.Data()[i]);

    if (val_first >= q2_i64 / 2) {
      val_first -= q2_i64;
    }
    WEAK_ENFORCE(i < rest_rows.Data().size());
    int64_t val_rest = static_cast<int64_t>(rest_rows.Data()[i]);
    if (val_rest >= q1_i64 / 2) {
      val_rest -= q1_i64;
    }

    auto denominator = static_cast<int64_t>(q2 * (q1 / p));
    int64_t r = val_first * q1_i64;
    r += (val_rest * q2_i64);

    // divide r by q2, rounding
    int64_t sign = r >= 0 ? 1 : -1;
    int64_t tmp = r + sign * (denominator / 2);
    auto res = static_cast<int128_t>(tmp / denominator);

    // int64_t can directly compute with int128_t
    int128_t middle = denominator / p_i128;
    int128_t right = 2 * p_i128;
    res = (res + middle + right) % p_i128;

    // get the low 64-bit
    // WEAK_ENFORCE(idx < result.Data().size());
    result.Data()[i] = yacl::DecomposeInt128(res).second;
  }

  return result;
}

PolyMatrixRaw SpiralClient::DecodeResponseInternal(
    const std::vector<PolyMatrixRaw>& response) const {
  uint64_t p = params_.PtModulus();
  uint64_t q1 = 4 * p;
  uint64_t q2 = kQ2Values[params_.Q2Bits()];
  size_t n = params_.N();
  size_t poly_len = params_.PolyLen();

  auto sk_gsw_q2 = PolyMatrixRaw::Zero(q2_params_.PolyLen(), n, 1);
  for (size_t i = 0; i < poly_len * n; ++i) {
    sk_gsw_q2.Data()[i] =
        arith::Recenter(sk_gsw_.Data()[i], params_.Modulus(), q2);
  }
  // for mul
  auto sk_gsw_q2_ntt =
      PolyMatrixNtt::Zero(q2_params_.CrtCount(), q2_params_.PolyLen(), n, 1);
  ToNtt(q2_params_, sk_gsw_q2_ntt, sk_gsw_q2);

  auto result =
      PolyMatrixRaw::Zero(params_.PolyLen(), params_.Instances() * n, n);

  for (size_t ins = 0; ins < params_.Instances(); ++ins) {
    auto packed_ct = response[ins];
    // \hat c_1
    auto first_row = packed_ct.SubMatrix(0, 0, 1, packed_ct.Cols());
    // \hat c_2
    auto rest_rows =
        packed_ct.SubMatrix(1, 0, packed_ct.Rows() - 1, packed_ct.Cols());
    // (1, n)
    auto first_row_q2 =
        PolyMatrixNtt::Zero(q2_params_.CrtCount(), q2_params_.PolyLen(), 1, n);
    ToNtt(q2_params_, first_row_q2, first_row);
    // \tilde s \hat c_1^T
    auto sk_prod =
        FromNtt(q2_params_, Multiply(q2_params_, sk_gsw_q2_ntt, first_row_q2));

    auto q1_i64 = static_cast<int64_t>(q1);
    auto q2_i64 = static_cast<int64_t>(q2);
    int128_t p_i128 = yacl::MakeInt128(0, p);

    for (size_t i = 0; i < n * n * poly_len; ++i) {
      WEAK_ENFORCE(i < sk_prod.Data().size());
      auto val_first = static_cast<int64_t>(sk_prod.Data()[i]);

      if (val_first >= q2_i64 / 2) {
        val_first -= q2_i64;
      }
      WEAK_ENFORCE(i < rest_rows.Data().size());
      int64_t val_rest = static_cast<int64_t>(rest_rows.Data()[i]);
      if (val_rest >= q1_i64 / 2) {
        val_rest -= q1_i64;
      }

      auto denominator = static_cast<int64_t>(q2 * (q1 / p));
      int64_t r = val_first * q1_i64;
      r += (val_rest * q2_i64);

      // divide r by q2, rounding
      int64_t sign = r >= 0 ? 1 : -1;
      int64_t tmp = r + sign * (denominator / 2);
      auto res = static_cast<int128_t>(tmp / denominator);

      // int64_t can directly compute with int128_t
      int128_t middle = denominator / p_i128;
      int128_t right = 2 * p_i128;
      res = (res + middle + right) % p_i128;
      size_t idx = ins * n * n * poly_len + i;

      // get the low 64-bit
      WEAK_ENFORCE(idx < result.Data().size());
      result.Data()[idx] = yacl::DecomposeInt128(res).second;
    }
  }

  return result;
}

// util methods
void ReorientRegCiphertexts(const Params& params, std::vector<uint64_t>& out,
                            const std::vector<PolyMatrixNtt>& v_reg) {
  size_t poly_len = params.PolyLen();
  size_t crt_count = params.CrtCount();

  WEAK_ENFORCE(crt_count == static_cast<size_t>(2));
  WEAK_ENFORCE(arith::Log2(params.Moduli(0)) <= static_cast<uint64_t>(32));

  size_t num_reg_expanded = 1 << params.DbDim1();
  size_t ct_rows = v_reg[0].Rows();
  size_t ct_cols = v_reg[0].Cols();

  WEAK_ENFORCE(ct_rows == static_cast<size_t>(2));
  WEAK_ENFORCE(ct_cols == static_cast<size_t>(1));

  uint64_t moduli0 = params.Moduli(0);
  uint64_t moduli1 = params.Moduli(1);

  // the improvment is about 15ms, compared the naive for-loop
  yacl::parallel_for(0, num_reg_expanded, [&](size_t begin, size_t end) {
    for (size_t j = begin; j < end; ++j) {
      for (size_t r = 0; r < ct_rows; ++r) {
        for (size_t m = 0; m < ct_cols; ++m) {
          for (size_t z = 0; z < poly_len; ++z) {
            size_t idx_in = r * (ct_cols * crt_count * poly_len) +
                            m * (crt_count * poly_len);
            size_t idx_out = z * (num_reg_expanded * ct_cols * ct_rows) +
                             j * (ct_cols * ct_rows) + m * ct_rows + r;

            uint64_t val1 = v_reg[j].Data()[idx_in + z] % moduli0;
            uint64_t val2 = v_reg[j].Data()[idx_in + poly_len + z] % moduli1;
            out[idx_out] = val1 | (val2 << 32);
          }
        }
      }
    }
  });
}

}  // namespace psi::spiral