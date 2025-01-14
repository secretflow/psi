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

#include <chrono>
#include <limits>
#include <optional>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/gadget.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/spiral_client.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {
class SpiralServerDerive : public SpiralServer {
 public:
  using SpiralServer::CoefficientExpansion;
  using SpiralServer::ExpandQuery;
  using SpiralServer::FoldCiphertexts;
  using SpiralServer::GetVFoldingNeg;
  using SpiralServer::MultiplyRegByDatabase;
  using SpiralServer::RegevToGsw;

  explicit SpiralServerDerive(Params params,
                              std::vector<uint64_t> reoriented_db)
      : psi::spiral::SpiralServer(std::move(params), std::move(reoriented_db)) {
  }

  explicit SpiralServerDerive(Params params,
                              std::vector<uint64_t> reoriented_db,
                              DatabaseMetaInfo database_info)
      : SpiralServer(std::move(params), std::move(reoriented_db),
                     std::move(database_info)) {}

  explicit SpiralServerDerive(Params params, DatabaseMetaInfo database_info)
      : SpiralServer(std::move(params), std::move(database_info)) {}
};

class SpiralClientDerive : public SpiralClient {
 public:
  using SpiralClient::GenQueryInternal;

  using SpiralClient::DecodeResponseInternal;

  using SpiralClient::DecryptMatrixRegev;

  using SpiralClient::DecryptMatrixGsw;

  using SpiralClient::EncryptMatrixGsw;

  using SpiralClient::EncryptMatrixRegev;

  explicit SpiralClientDerive(Params params, DatabaseMetaInfo database_info)
      : SpiralClient(std::move(params), std::move(database_info)) {}

  explicit SpiralClientDerive(Params params)
      : SpiralClient(std::move(params)) {}
};

[[maybe_unused]] constexpr size_t kMaxLoop{1};

[[maybe_unused]] uint64_t DecGsw(const Params& params, const PolyMatrixNtt& ct,
                                 SpiralClientDerive& client) {
  auto dec = FromNtt(params, client.DecryptMatrixRegev(ct));

  size_t idx = 2 * (params.TGsw() - 1) * params.PolyLen() + params.PolyLen();
  int64_t val = static_cast<int64_t>(dec.Data()[idx]);

  if (val >= static_cast<int64_t>(params.Modulus() >> 1)) {
    val -= static_cast<int64_t>(params.Modulus());
  }
  if (std::abs(val) < static_cast<int64_t>(1 << 10)) {
    return 0;
  } else {
    return 1;
  }
}

[[maybe_unused]] uint64_t DecRegev(const Params& params,
                                   const PolyMatrixNtt& ct,
                                   SpiralClientDerive& client,
                                   uint64_t scale_k) {
  auto dec = FromNtt(params, client.DecryptMatrixRegev(ct));

  int64_t val = static_cast<int64_t>(dec.Data()[0]);

  if (val >= static_cast<int64_t>(params.Modulus() >> 1)) {
    val -= static_cast<int64_t>(params.Modulus());
  }
  auto val_rounded = static_cast<int64_t>(
      std::round(static_cast<double>(val) / static_cast<double>(scale_k)));
  SPDLOG_DEBUG("dec.data[0]: {}, val: {}, val_rounded: {}", dec.Data()[0], val,
               val_rounded);

  if (val_rounded == 0) {
    return 0;

  } else {
    return 1;
  }
}

void FullProtocolIsCorrect(Params&& params, Params&& params_client,
                           Params&& params_server) {
  yacl::crypto::Prg<uint64_t> prg;
  size_t target_idx = prg() % (1 << (params.DbDim1() + params.DbDim2()));

  // new client
  SpiralClientDerive client(std::move(params_client));
  auto pks = client.GenKeys();

  // genquery
  auto query = client.GenQueryInternal(target_idx);
  // random database
  auto [correct_item, db] = GenRandomDbAndGetItem(params, target_idx);

  // new server
  SpiralServer server(std::move(params_server), db);
  server.SetPublicKeys(std::move(pks));

  // gen response
  auto response = server.ProcessQuery(query);

  // decode
  auto result = client.DecodeResponseInternal(response);

  // verify
  EXPECT_EQ(correct_item.Data(), result.Data());
}

}  // namespace

TEST(FullProtocol, FastExpansionTestingParam) {
  auto params = util::GetFastExpansionTestingParam();
  auto params1 = util::GetFastExpansionTestingParam();
  auto params2 = util::GetFastExpansionTestingParam();

  SPDLOG_DEBUG("params: {}", params.ToString());

  FullProtocolIsCorrect(std::move(params), std::move(params1),
                        std::move(params2));
}

TEST(SpiralServer, MultiplyRegByDatabase) {
  auto params = util::GetFastExpansionTestingParam();
  auto params_client = util::GetFastExpansionTestingParam();
  auto params_server = util::GetFastExpansionTestingParam();

  yacl::crypto::Prg<uint64_t> rng(yacl::crypto::SecureRandU128());
  yacl::crypto::Prg<uint64_t> rng_pub;

  size_t dim0 = 1 << params.DbDim1();
  size_t num_per = 1 << params.DbDim2();
  uint64_t scale_k = params.ScaleK();

  size_t target_idx = rng() % (dim0 * num_per);
  size_t target_idx_dim0 = target_idx / num_per;
  size_t target_idx_num_per = target_idx % num_per;
  // new client
  SpiralClientDerive client(std::move(params_client));
  auto pp = client.GenKeys();
  // random db
  auto [corr_item, db] = GenRandomDbAndGetItem(params, target_idx);

  // query ciphertext
  std::vector<PolyMatrixNtt> v_reg;
  for (size_t i = 0; i < dim0; ++i) {
    uint64_t val = (i == target_idx_dim0) ? scale_k : 0ULL;
    auto sigma =
        ToNtt(params, PolyMatrixRaw::SingleValue(params.PolyLen(), val));
    v_reg.push_back(client.EncryptMatrixRegev(sigma, rng, rng_pub));
  }

  size_t v_reg_sz = dim0 * 2 * params.PolyLen();
  std::vector<uint64_t> v_reg_reoriented(v_reg_sz);
  ReorientRegCiphertexts(params, v_reg_reoriented, v_reg);

  std::vector<PolyMatrixNtt> out;
  out.reserve(num_per);
  for (size_t i = 0; i < num_per; ++i) {
    out.push_back(
        PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1));
  }
  SpiralServerDerive server(std::move(params_server), std::move(db));
  server.SetPublicKeys(std::move(pp));
  // now mul
  server.MultiplyRegByDatabase(out, v_reg_reoriented, dim0, num_per);
  // decrypt
  auto dec =
      FromNtt(params, client.DecryptMatrixRegev(out[target_idx_num_per]));

  auto dec_rescaled = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  for (size_t z = 0; z < params.PolyLen(); ++z) {
    dec_rescaled.Data()[z] =
        arith::Rescale(dec.Data()[z], params.Modulus(), params.PtModulus());
  }
  // veryfiy
  for (size_t z = 0; z < params.PolyLen(); ++z) {
    ASSERT_EQ(dec_rescaled.Data()[z], corr_item.Data()[z]);
  }
}

TEST(SpiralServer, CoefficientExpansion) {
  auto params = util::GetFastExpansionTestingParam();
  auto params_client = util::GetFastExpansionTestingParam();
  auto params_server = util::GetFastExpansionTestingParam();

  auto v_neg1 = GetVneg1(params);

  yacl::crypto::Prg<uint64_t> rng;
  yacl::crypto::Prg<uint64_t> rng_pub;

  SpiralClientDerive client(std::move(params_client));
  auto pp = client.GenKeys();

  std::vector<PolyMatrixNtt> v(1 << (params.DbDim1() + 1));
  for (size_t i = 0; i < v.size(); ++i) {
    v[i] = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2, 1);
  }
  uint64_t scale_k = params.ScaleK();

  // random idx
  size_t idx_target = rng() % v.size();
  PolyMatrixRaw sigma = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  sigma.Data()[idx_target] = scale_k;
  auto sigma_ntt = ToNtt(params, sigma);
  // one ciphertext
  v[0] = client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);

  YACL_ENFORCE(pp.v_expansion_left_.size() > 0);
  YACL_ENFORCE(pp.v_expansion_right_.size() > 0);

  auto v_w_left = pp.v_expansion_left_;
  auto v_w_right = pp.v_expansion_right_;

  // init server
  std::vector<uint64_t> db;
  SpiralServerDerive server(std::move(params_server), std::move(db));
  server.SetPublicKeys(std::move(pp));
  // expand to mutli ciphertext
  server.CoefficientExpansion(v, params.G(), params.StopRound(), v_w_left,
                              v_w_right, v_neg1,
                              params.TGsw() * params.DbDim2());

  for (size_t i = 0; i < v.size(); ++i) {
    if (i == idx_target) {
      auto decrypt = DecRegev(params, v[i], client, scale_k);
      ASSERT_EQ(1, decrypt);
    } else {
      auto decrypt = DecRegev(params, v[i], client, scale_k);
      ASSERT_EQ(0, decrypt) << "i: " << i << ", decrypted v[i] error \n";
    }
  }
}

TEST(SpiralServer, RegevToGsw) {
  auto params = util::GetFastExpansionTestingParam();
  auto params_client = util::GetFastExpansionTestingParam();
  auto params_server = util::GetFastExpansionTestingParam();

  yacl::crypto::Prg<uint64_t> rng(yacl::crypto::SecureRandU128());
  yacl::crypto::Prg<uint64_t> rng_pub;

  // new client
  SpiralClientDerive client(std::move(params_client));
  auto pp = client.GenKeys();

  // function
  auto enc_constant = [&](uint64_t val) {
    auto sigma = PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
    sigma.Data()[0] = val;
    auto sigma_ntt = ToNtt(params, sigma);
    return client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);
  };

  auto& v = pp.v_conversion_[0];
  auto bits_per = util::GetBitsPer(params, params.TGsw());

  std::vector<PolyMatrixNtt> v_inp_1;
  std::vector<PolyMatrixNtt> v_inp_0;

  for (size_t i = 0; i < params.TGsw(); ++i) {
    uint64_t val = 1ULL << (bits_per * i);
    v_inp_1.emplace_back(enc_constant(val));
    v_inp_0.emplace_back(enc_constant(0ULL));
  }

  std::vector<PolyMatrixNtt> v_gsw;
  v_gsw.emplace_back(PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2,
                                         2 * params.TGsw()));

  // new Server
  std::vector<uint64_t> db;
  SpiralServerDerive server(std::move(params_server), std::move(db));
  server.SetPublicKeys(std::move(pp));
  // regev to gsw
  server.RegevToGsw(v_gsw, v_inp_1, v, 1, 0);
  EXPECT_EQ(1, DecGsw(params, v_gsw[0], client));

  server.RegevToGsw(v_gsw, v_inp_0, v, 1, 0);
  EXPECT_EQ(0, DecGsw(params, v_gsw[0], client));
}

TEST(SpiralServer, FoldCiphertexts) {
  auto params = util::GetFastExpansionTestingParam();
  auto params_client = util::GetFastExpansionTestingParam();
  auto params_server = util::GetFastExpansionTestingParam();

  yacl::crypto::Prg<uint64_t> rng(yacl::crypto::SecureRandU128());
  yacl::crypto::Prg<uint64_t> rng_pub;

  size_t dim0 = 1 << params.DbDim1();
  size_t num_per = 1 << params.DbDim2();
  uint64_t scale_k = params.ScaleK();

  size_t targrt_idx = rng() % (dim0 * num_per);
  size_t target_idx_num_per = targrt_idx % num_per;
  // new client
  SpiralClientDerive client(std::move(params_client));
  auto pp = client.GenKeys();
  auto query = client.GenQueryInternal(targrt_idx);

  std::vector<PolyMatrixNtt> v_reg;
  for (size_t i = 0; i < num_per; ++i) {
    uint64_t val = (i == target_idx_num_per) ? scale_k : 0ULL;
    auto sigma =
        ToNtt(params, PolyMatrixRaw::SingleValue(params.PolyLen(), val));
    v_reg.push_back(client.EncryptMatrixRegev(sigma, rng, rng_pub));
  }

  std::vector<PolyMatrixRaw> v_regev_raw;
  for (auto& v_reg : v_reg) {
    v_regev_raw.push_back(FromNtt(params, v_reg));
  }
  size_t bits_per = util::GetBitsPer(params, params.TGsw());
  std::vector<PolyMatrixNtt> v_folding;

  auto sk_regev_ntt = ToNtt(params, client.GetSkRegev());
  for (size_t i = 0; i < params.DbDim2(); ++i) {
    uint64_t bit = static_cast<uint64_t>(target_idx_num_per & (1ULL << i)) >> i;

    auto ct_gsw = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(), 2,
                                      2 * params.TGsw());

    for (size_t j = 0; j < params.TGsw(); ++j) {
      uint64_t value = (1ULL << (bits_per * j)) * bit;

      auto sigma = PolyMatrixRaw::SingleValue(params.PolyLen(), value);
      auto sigma_ntt = ToNtt(params, sigma);
      auto ct = client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);
      ct_gsw.CopyInto(ct, 0, 2 * j + 1);
      auto prod = Multiply(params, sk_regev_ntt, sigma_ntt);
      auto ct1 = client.EncryptMatrixRegev(prod, rng, rng_pub);
      ct_gsw.CopyInto(ct1, 0, 2 * j);
    }
    v_folding.push_back(ct_gsw);
  }

  auto gadget_ntt =
      ToNtt(params, util::BuildGadget(params, 2, 2 * params.TGsw()));

  std::vector<PolyMatrixNtt> v_folding_neg;
  auto ct_gsw_inv = PolyMatrixRaw::Zero(params.PolyLen(), 2, 2 * params.TGsw());

  for (size_t i = 0; i < params.DbDim2(); ++i) {
    Invert(params, ct_gsw_inv, FromNtt(params, v_folding[i]));

    auto ct_gsw_neg = PolyMatrixNtt::Zero(params.CrtCount(), params.PolyLen(),
                                          2, 2 * params.TGsw());
    Add(params, ct_gsw_neg, gadget_ntt, ToNtt(params, ct_gsw_inv));

    v_folding_neg.push_back(ct_gsw_neg);
  }

  // new Server
  std::vector<uint64_t> db;
  SpiralServerDerive server(std::move(params_server), std::move(db));
  server.SetPublicKeys(std::move(pp));
  // now folding
  server.FoldCiphertexts(v_regev_raw, v_folding, v_folding_neg);

  ASSERT_EQ(1,
            DecRegev(params, ToNtt(params, v_regev_raw[0]), client, scale_k));
}

}  // namespace psi::spiral