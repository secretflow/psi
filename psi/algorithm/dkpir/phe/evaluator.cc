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

#include "psi/algorithm/dkpir/phe/evaluator.h"

namespace psi::dkpir::phe {
Ciphertext Evaluator::Add(const Ciphertext& ct1, const Ciphertext& ct2,
                          const std::shared_ptr<yacl::crypto::EcGroup>& curve) {
  yacl::crypto::EcPoint c0 = curve->Add(ct1.GetC0(), ct2.GetC0());
  yacl::crypto::EcPoint c1 = curve->Add(ct1.GetC1(), ct2.GetC1());
  return Ciphertext(c0, c1);
}

bool Evaluator::Check(const Ciphertext& ct, const yacl::math::MPInt& m,
                      const SecretKey& sk) {
  std::shared_ptr<yacl::crypto::EcGroup> curve = sk.GetCurve();

  yacl::crypto::EcPoint c0 = ct.GetC0();
  yacl::crypto::EcPoint c1 = ct.GetC1();

  yacl::crypto::EcPoint res = curve->MulDoubleBase(m, sk.GetSk(), c0);

  return curve->PointEqual(c1, res);
}
}  // namespace psi::dkpir::phe