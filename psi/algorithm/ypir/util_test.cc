#include "../spiral/util.h"

#include "../spiral/arith/arith.h"
#include "../spiral/arith/number_theory.h"
#include "../spiral/gadget.h"
#include "../spiral/poly_matrix.h"
#include "../spiral/poly_matrix_utils.h"
#include "../spiral/public_keys.h"
#include "../spiral/spiral_client.h"
#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/ypir/util.h"

namespace psi::ypir {
TEST(RLWETest, EncDecTest) {
  auto params = spiral::util::GetFastExpansionTestingParam();

  uint64_t p = params.PtModulus();
  uint64_t q = params.Modulus();
  uint64_t scale_k = params.ScaleK();
  auto sigma_raw = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  auto pt_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> pt_rng(pt_seed);
  std::vector<uint64_t> m_enc(params.PolyLen());
  for (size_t i = 0; i < params.PolyLen(); ++i) {
    m_enc[i] = pt_rng() % p;
    sigma_raw.Data()[i] = (m_enc[i] * scale_k) % q;
  }
  auto sigma_ntt = spiral::ToNtt(params, sigma_raw);

  class ClientDerive : public spiral::SpiralClient {
   public:
    using spiral::SpiralClient::DecryptMatrixRegev;
    using spiral::SpiralClient::EncryptMatrixRegev;
    using spiral::SpiralClient::SpiralClient;
  };
  ClientDerive client(params);

  auto seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> rng(seed);
  auto pub_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> rng_pub(pub_seed);

  auto ct = client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);
  auto dec_ntt = client.DecryptMatrixRegev(ct);
  auto dec_raw = spiral::FromNtt(params, dec_ntt);

  ASSERT_EQ(dec_raw.Data().size(), sigma_raw.Data().size());
  for (size_t i = 0; i < sigma_raw.Data().size(); ++i) {
    uint64_t v_rescaled = spiral::arith::Rescale(
        dec_raw.Data()[i], params.Modulus(), params.PtModulus());
    uint64_t v_exp = m_enc[i];
    ASSERT_EQ(v_rescaled, v_exp) << "mismatch at coeff " << i;
  }
}

TEST(RLWETest, AutomorphismTest) {
  auto params = spiral::util::GetFastExpansionTestingParam();

  uint64_t p = params.PtModulus();
  uint64_t q = params.Modulus();
  uint64_t scale_k = params.ScaleK();
  auto sigma_raw = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  auto pt_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> pt_rng(pt_seed);
  std::vector<uint64_t> m_enc(params.PolyLen());
  for (size_t i = 0; i < params.PolyLen(); ++i) {
    m_enc[i] = pt_rng() % p;
    sigma_raw.Data()[i] = (m_enc[i] * scale_k) % q;
  }
  auto sigma_ntt = spiral::ToNtt(params, sigma_raw);

  class ClientDerive : public spiral::SpiralClient {
   public:
    using spiral::SpiralClient::DecryptMatrixRegev;
    using spiral::SpiralClient::EncryptMatrixRegev;
    using spiral::SpiralClient::GenPublicKeys;
    using spiral::SpiralClient::SpiralClient;
  };
  ClientDerive client(params);
  auto pp = client.GenPublicKeys();

  auto seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> rng(seed);
  auto pub_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> rng_pub(pub_seed);

  auto ct = client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);

  for (size_t i = 0; i < pp.v_expansion_right_.size(); ++i) {
    size_t t = (params.PolyLen() / (1 << i)) + 1;
    size_t t_exp = params.TExpRight();

    const auto& pub_param = pp.v_expansion_right_[i];

    auto ct_auto_ntt = HomomorphicAutomorph(params, t, t_exp, ct, pub_param);

    auto dec_auto_ntt = client.DecryptMatrixRegev(ct_auto_ntt);
    auto dec_auto_raw = spiral::FromNtt(params, dec_auto_ntt);

    auto sigma_auto_raw = spiral::Automorphism(params, sigma_raw, t);

    auto sigma_auto_ntt = spiral::ToNtt(params, sigma_auto_raw);

    ASSERT_EQ(dec_auto_raw.Data().size(), sigma_auto_raw.Data().size());
    for (size_t j = 0; j < sigma_auto_raw.Data().size(); ++j) {
      uint64_t v_rescaled = spiral::arith::Rescale(
          dec_auto_raw.Data()[j], params.Modulus(), params.PtModulus());
      uint64_t v_exp_scaled = sigma_auto_raw.Data()[j];
      uint64_t v_exp = spiral::arith::Rescale(v_exp_scaled, params.Modulus(),
                                              params.PtModulus());
      ASSERT_EQ(v_rescaled, v_exp)
          << "mismatch at coeff " << j << " for expansion level " << i;
    }
  }
}

TEST(RLWETest, PackingTest) {
  auto params = spiral::util::GetFastExpansionTestingParam();

  class ClientDerive : public spiral::SpiralClient {
   public:
    using spiral::SpiralClient::DecryptMatrixRegev;
    using spiral::SpiralClient::EncryptMatrixRegev;
    using spiral::SpiralClient::GenPublicKeys;
    using spiral::SpiralClient::GetSkRegev;
    using spiral::SpiralClient::SpiralClient;
  };
  ClientDerive client(params);
  auto pp = client.GenPublicKeys();

  auto y_constants = GenYConstants(params);

  auto pack_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> pack_rng(yacl::crypto::SecureRandU128());
  yacl::crypto::Prg<uint64_t> pack_rng_pub(pack_seed);
  auto pack_pub_params = RawGenerateExpansionParams(
      params, client.GetSkRegev(), params.PolyLenLog2(), params.TExpLeft(),
      pack_rng, pack_rng_pub);

  auto cts_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> ct_rng(cts_seed);
  auto ct_pub_seed = yacl::crypto::SecureRandU128();
  yacl::crypto::Prg<uint64_t> ct_rng_pub(ct_pub_seed);

  std::vector<spiral::PolyMatrixNtt> v_ct;
  std::vector<uint64_t> b_values;
  v_ct.reserve(params.PolyLen());
  b_values.reserve(params.PolyLen());

  uint64_t p = params.PtModulus();
  uint64_t scale_k = params.ScaleK();
  uint64_t mod_inv = spiral::arith::InvertUintMod(
      static_cast<uint64_t>(params.PolyLen()), params.Modulus());

  for (size_t i = 0; i < params.PolyLen(); ++i) {
    auto pt = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
    uint64_t val = i % p;
    uint64_t val_to_enc = spiral::arith::MultiplyUintMod(val * scale_k, mod_inv,
                                                         params.Modulus());
    pt.Data()[0] = val_to_enc;

    auto pt_ntt = spiral::ToNtt(params, pt);
    auto ct = client.EncryptMatrixRegev(pt_ntt, ct_rng, ct_rng_pub);
    auto ct_raw = spiral::FromNtt(params, ct);

    b_values.push_back(ct_raw.Data()[ct_raw.PolyStartIndex(1, 0)]);

    std::fill(
        ct_raw.Data().begin() + ct_raw.PolyStartIndex(1, 0),
        ct_raw.Data().begin() + ct_raw.PolyStartIndex(1, 0) + params.PolyLen(),
        0ULL);
    v_ct.push_back(spiral::ToNtt(params, ct_raw));
  }

  auto packed = RingPackLwes(params, b_values, v_ct, params.PolyLen(),
                             pack_pub_params, y_constants);

  auto dec = client.DecryptMatrixRegev(packed);
  auto dec_raw = spiral::FromNtt(params, dec);

  auto rescaled = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  for (size_t i = 0; i < params.PolyLen(); ++i) {
    rescaled.Data()[i] = spiral::arith::Rescale(
        dec_raw.Data()[i], params.Modulus(), params.PtModulus());
  }

  auto gold = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
  for (size_t i = 0; i < params.PolyLen(); ++i) {
    gold.Data()[i] = i % p;
  }

  ASSERT_EQ(rescaled.Data().size(), gold.Data().size());
  for (size_t i = 0; i < gold.Data().size(); ++i) {
    ASSERT_EQ(rescaled.Data()[i], gold.Data()[i]) << "mismatch at coeff " << i;
  }
}

}  // namespace psi::ypir