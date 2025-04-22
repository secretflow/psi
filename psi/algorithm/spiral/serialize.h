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

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "yacl/base/buffer.h"

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/public_keys.h"
// #include "psi/algorithm/spiral/spiral_client.h"

#include "psi/algorithm/spiral/serializable.pb.h"

namespace psi::spiral {

yacl::Buffer SerializePolyMatrixRaw(const PolyMatrixRaw& poly_matrix);
std::string SerializePolyMatrixRawToStr(const PolyMatrixRaw& poly_matrix);

PolyMatrixRaw DeserializePolyMatrixRaw(const Params& params,
                                       const yacl::ByteContainerView& buffer);

// the first row we use a seed to compress
yacl::Buffer SerializePolyMatrixRawRng(const PolyMatrixRaw& poly_matrix,
                                       const Params& params);
std::string SerializePolyMatrixRawRngToStr(const PolyMatrixRaw& poly_matrix,
                                           const Params& params);

PolyMatrixRaw DeserializePolyMatrixRawRng(yacl::ByteContainerView buffer,
                                          const Params& params,
                                          yacl::crypto::Prg<uint64_t> rng);

yacl::Buffer SerializeResponse(const std::vector<PolyMatrixRaw>& responses);
std::string SerializeResponseToStr(const std::vector<PolyMatrixRaw>& responses);
std::vector<PolyMatrixRaw> DeserializeResponse(
    const Params& params, const yacl::ByteContainerView& buffer);

yacl::Buffer SerializePublicKeys(const Params& params, const PublicKeys& pks);
std::string SerializePublicKeysToStr(const Params& params,
                                     const PublicKeys& pks);
PublicKeys DeserializePublicKeys(const Params& params,
                                 const yacl::ByteContainerView& buffer);

}  // namespace psi::spiral