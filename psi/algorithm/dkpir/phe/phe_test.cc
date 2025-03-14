// Copyright 2025
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

#include "psi/algorithm/dkpir/phe/phe.h"

#include <vector>

#include "gtest/gtest.h"
#include "yacl/crypto/rand/rand.h"

namespace psi::dkpir::phe {

namespace {

TEST(PheTest, Works) {
  std::shared_ptr<yacl::crypto::EcGroup> curve =
      yacl::crypto::EcGroupFactory::Instance().Create(
          "fourq", yacl::ArgLib = "FourQlib");
  PublicKey public_key;
  SecretKey secret_key;
  KeyGenerator::GenerateKey(curve, &public_key, &secret_key);
  Encryptor encryptor(public_key);

  uint64_t point_size = curve->GetSerializeLength();

  // Check if the public and private keys are generated correctly
  {
    yacl::crypto::EcPoint pk_point = public_key.GetPk();
    yacl::math::MPInt sk = secret_key.GetSk();
    EXPECT_TRUE(curve->PointEqual(pk_point, curve->MulBase(sk)));
  }

  // Check whether the encryption and serialization are correct
  {
    yacl::math::MPInt m(yacl::crypto::SecureRandU32());
    Ciphertext ct = encryptor.Encrypt(m);
    EXPECT_TRUE(Evaluator::Check(ct, m, secret_key));

    std::vector<uint8_t> buf(point_size * 2);
    ct.SerializeCiphertext(curve, buf.data(), buf.size());
    Ciphertext ct_copy;
    ct_copy.DeserializeCiphertext(curve, buf.data(), buf.size());

    EXPECT_TRUE(curve->PointEqual(ct_copy.GetC0(), ct.GetC0()));
    EXPECT_TRUE(curve->PointEqual(ct_copy.GetC1(), ct.GetC1()));
  }

  // Check whether the homomorphic addition is correct
  {
    yacl::math::MPInt m1(yacl::crypto::SecureRandU32());
    yacl::math::MPInt m2(yacl::crypto::SecureRandU32());
    yacl::math::MPInt m3 = m1 + m2;

    Ciphertext ct1 = encryptor.Encrypt(m1);
    Ciphertext ct2 = encryptor.Encrypt(m2);
    Ciphertext ct3 = Evaluator::Add(ct1, ct2, curve);

    EXPECT_TRUE(Evaluator::Check(ct3, m3, secret_key));
  }
}

}  // namespace
}  // namespace psi::dkpir::phe