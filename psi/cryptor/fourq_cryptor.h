// Copyright 2022 Ant Group Co., Ltd.
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

#pragma once

#include "yacl/base/exception.h"

#include "psi/cryptor/ecc_cryptor.h"

namespace psi {

class FourQEccCryptor : public IEccCryptor {
 public:
  FourQEccCryptor() {
    ec_group_ = yacl::crypto::EcGroupFactory::Instance().Create(
        "FourQ", yacl::ArgLib = "FourQlib");
  };

  ~FourQEccCryptor() override = default;

  CurveType GetCurveType() const override { return CurveType::CURVE_FOURQ; }

  yacl::crypto::EcPoint HashToCurve(
      absl::Span<const char> input) const override;
};

}  // namespace psi