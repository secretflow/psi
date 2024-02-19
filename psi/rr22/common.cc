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

#include "psi/rr22/common.h"

#include "omp.h"

#include "psi/utils/bucket.h"

namespace psi::rr22 {

Rr22PsiOptions GenerateRr22PsiOptions(bool low_comm_mode) {
  Rr22PsiOptions options(kDefaultSSP, omp_get_num_procs(), kDefaultCompress);
  options.mode =
      low_comm_mode ? Rr22PsiMode::LowCommMode : Rr22PsiMode::FastMode;

  return options;
}

void CommonInit(const std::string& key_hash_digest, v2::PsiConfig* config,
                RecoveryManager* recovery_manager) {
  if (config->protocol_config().rr22_config().bucket_size() == 0) {
    config->mutable_protocol_config()->mutable_rr22_config()->set_bucket_size(
        kDefaultBucketSize);
  }

  if (recovery_manager) {
    recovery_manager->MarkInitEnd(*config, key_hash_digest);
  }
}

}  // namespace psi::rr22
