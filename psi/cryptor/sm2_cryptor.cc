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

#include "psi/cryptor/sm2_cryptor.h"

#include "absl/types/span.h"

namespace psi {

yacl::crypto::EcPoint Sm2Cryptor::HashToCurve(
    absl::Span<const char> item_data) const {
  return ec_group_->HashToCurve(
      yacl::crypto::HashToCurveStrategy::TryAndRehash_SHA2,
      std::string_view(item_data.data(), item_data.size()));
}

}  // namespace psi
