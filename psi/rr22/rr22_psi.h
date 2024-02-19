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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "yacl/base/exception.h"
#include "yacl/base/int128.h"
#include "yacl/link/context.h"

#include "psi/rr22/rr22_oprf.h"

// [RR22] Blazing Fast PSI from Improved OKVS and Subfield VOLE, CCS 2022
// https://eprint.iacr.org/2022/320
// okvs code reference https://github.com/Visa-Research/volepsi

namespace psi::rr22 {

struct Rr22PsiOptions {
  Rr22PsiOptions(size_t ssp_params, size_t num_threads_params,
                 bool compress_params, bool malicious_params = false)
      : ssp(ssp_params),
        num_threads(num_threads_params),
        compress(compress_params),
        malicious(malicious_params) {
    YACL_ENFORCE(ssp >= 30, "ssp:{}", ssp);

    num_threads = std::max<size_t>(1, num_threads_params);
  }

  // ssp i.e. statistical security parameter.
  // must >= 30bit
  size_t ssp = 30;

  // number of threads
  // value 0 will use 1 thread
  size_t num_threads = 0;

  // psi mode: fast or low communication
  Rr22PsiMode mode = Rr22PsiMode::FastMode;

  // wether compress the OPRF outputs
  bool compress = true;

  // run the protocol with malicious security
  // not supported by now
  bool malicious = false;

  yacl::crypto::CodeType code_type = yacl::crypto::CodeType::Silver5;
};

void Rr22PsiSenderInternal(const Rr22PsiOptions& options,
                           const std::shared_ptr<yacl::link::Context>& lctx,
                           const std::vector<uint128_t>& inputs);

// return psi result indices,
// indices are not sorted; need to be sorted by caller.
std::vector<size_t> Rr22PsiReceiverInternal(
    const Rr22PsiOptions& options,
    const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<uint128_t>& inputs);

}  // namespace psi::rr22
