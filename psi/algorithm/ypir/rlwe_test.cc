#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "../spiral/gadget.h"
#include "../spiral/poly_matrix.h"
#include "../spiral/poly_matrix_utils.h"
#include "../spiral/public_keys.h"
#include "../spiral/util.h"
#include "../spiral/spiral_client.h"
#include "../spiral/arith/number_theory.h"
#include "psi/algorithm/ypir/util.h"

namespace psi::ypir {

// TEST(RLWETest, AllWorkflow) {
//   auto params = spiral::util::GetFastExpansionTestingParam();

//   size_t t_exp = params.TExpLeft();

//   uint64_t p = params.PtModulus();
//   uint64_t q = params.Modulus();
//   auto sigma_raw = spiral::PolyMatrixRaw::Zero(params.PolyLen(), 1, 1);
//   uint64_t scale_k = params.ScaleK();

//   auto pt_seed = yacl::crypto::SecureRandU128();
//   yacl::crypto::Prg<uint64_t> pt_rng(pt_seed);
//   std::vector<uint64_t> m_auto(params.PolyLen());
//   for (size_t i = 0; i < params.PolyLen(); ++i) {
//     m_auto[i] = pt_rng() % p;
//     sigma_raw.Data()[i] = (m_auto[i] * scale_k) % q;
//   }
//   auto sigma_ntt = spiral::ToNtt(params, sigma_raw);

//   class ClientDerive : public spiral::SpiralClient {
//    public:
//     using spiral::SpiralClient::SpiralClient;
//     using spiral::SpiralClient::EncryptMatrixRegev;
//     using spiral::SpiralClient::DecryptMatrixRegev;
//   };
//   ClientDerive client(params);
//   auto pks = client.GenPublicKeys();
//   ASSERT_FALSE(pks.v_expansion_left_.empty());
//   auto pub_param = pks.v_expansion_left_[0];

//   size_t t = (params.PolyLen() / 2) + 1;

//   auto seed = yacl::crypto::SecureRandU128();
//   yacl::crypto::Prg<uint64_t> rng(seed);
//   auto pub_seed = yacl::crypto::SecureRandU128();
//   yacl::crypto::Prg<uint64_t> rng_pub(pub_seed);
//   auto ct = client.EncryptMatrixRegev(sigma_ntt, rng, rng_pub);

//   auto out = psi::ypir::HomomorphicAutomorph(params, t, t_exp, ct, pub_param);
//   ASSERT_TRUE(out.IsNtt());
//   ASSERT_EQ(out.Rows(), static_cast<size_t>(2));
//   ASSERT_EQ(out.Cols(), static_cast<size_t>(1));

//   auto dec_ntt = client.DecryptMatrixRegev(ct);
//   auto dec_raw = spiral::FromNtt(params, dec_ntt);
//   auto expect = spiral::Automorphism(params, sigma_raw, t);
//   ASSERT_EQ(dec_raw.Data().size(), expect.Data().size());
//   for (size_t i = 0; i < expect.Data().size(); ++i) {
//     uint64_t v_dec_p = spiral::arith::Rescale(dec_raw.Data()[i], q, p);
//     uint64_t v_exp_p = spiral::arith::Rescale(expect.Data()[i], q, p);
//     ASSERT_EQ(v_dec_p, v_exp_p) << "mismatch at coeff " << i;
//   }
// }

TEST(RLWETest, EncDecCorrect) {
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
    using spiral::SpiralClient::SpiralClient;
    using spiral::SpiralClient::EncryptMatrixRegev;
    using spiral::SpiralClient::DecryptMatrixRegev;
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
    uint64_t v_rescaled = spiral::arith::Rescale(dec_raw.Data()[i], params.Modulus(), params.PtModulus());
    uint64_t v_exp = m_enc[i];
    ASSERT_EQ(v_rescaled, v_exp) << "mismatch at coeff " << i;
  }
}

}