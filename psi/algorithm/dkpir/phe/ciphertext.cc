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

#include "psi/algorithm/dkpir/phe/ciphertext.h"

namespace psi::dkpir::phe {
void Ciphertext::SerializeCiphertext(
    const std::shared_ptr<yacl::crypto::EcGroup>& curve, uint8_t* buf,
    uint64_t buf_size) const {
  uint64_t point_size = curve->GetSerializeLength();
  YACL_ENFORCE(buf_size == point_size * 2,
               "The length of ciphertext should be twice the length of point.");
  curve->SerializePoint(c0_, buf, point_size);
  curve->SerializePoint(c1_, buf + point_size, point_size);
}

void Ciphertext::DeserializeCiphertext(
    const std::shared_ptr<yacl::crypto::EcGroup>& curve, uint8_t* buf,
    uint64_t buf_size) {
  uint64_t point_size = curve->GetSerializeLength();
  YACL_ENFORCE(buf_size == point_size * 2,
               "The length of ciphertext should be twice the length of point.");
  c0_ = curve->DeserializePoint(absl::MakeSpan(buf, point_size));
  c1_ = curve->DeserializePoint(absl::MakeSpan(buf + point_size, point_size));
}
}  // namespace psi::dkpir::phe