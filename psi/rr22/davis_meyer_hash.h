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

#include "absl/types/span.h"
#include "yacl/base/int128.h"

namespace psi::rr22 {

//
// Reference:
// https://github.com/Visa-Research/volepsi/blob/main/volePSI/RsOprf.cpp#L451C22-L451C22
// Visa-Research/volepsi use davies-meyer Hash in malicious oprf.
//
// davies-meyer Hash Reference:
// https://www.csa.iisc.ac.in/~arpita/Cryptography15/CT5.1.pdf
// compute davies-meyer Hash
// H(u,v) = AES_u(v) ^ v
uint128_t DavisMeyerHash(uint128_t key, uint128_t value);

void DavisMeyerHash(absl::Span<const uint128_t> key,
                    absl::Span<const uint128_t> value,
                    absl::Span<uint128_t> outputs);

}  // namespace psi::rr22
