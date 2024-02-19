// Copyright 2023 Ant Group Co., Ltd.
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

#include <string>
#include <vector>

#include "yacl/base/byte_container_view.h"

namespace psi {

// RFC9380 Hashing to Elliptic Curves
// https://datatracker.ietf.org/doc/rfc9380/
// curve25519 Elligator 2 hash_to_curve
// buffer: a byte string containing the message to hash.
// dst: domain separation tag (DST) 16B-255B
// RFC9380 3.1.  Domain Separation Requirements
// A reasonable choice of tag is "QUUX-V<xx>-CS<yy>-<suiteID>", where
//   <xx> and <yy> are two-digit numbers indicating the version and
//   ciphersuite, respectively, and <suiteID> is the Suite ID of the
//  encoding used in ciphersuite <yy>
std::vector<uint8_t> HashToCurveElligator2(
    yacl::ByteContainerView buffer,
    const std::string &dst =
        "SECRETFLOW-V01-CS02-with-curve25519_XMD:SHA-512_ELL2_RO_");

}  // namespace psi
