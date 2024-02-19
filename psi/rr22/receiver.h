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

#include "psi/interface.h"
#include "psi/utils/hash_bucket_cache.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi::rr22 {

class Rr22PsiReceiver final : public AbstractPsiReceiver {
 public:
  explicit Rr22PsiReceiver(const v2::PsiConfig &config,
                           std::shared_ptr<yacl::link::Context> lctx = nullptr);

  ~Rr22PsiReceiver() override = default;

 private:
  void Init() override;

  void PreProcess() override;

  void Online() override;

  void PostProcess() override;

  uint64_t bucket_count_ = 0;

  std::unique_ptr<HashBucketCache> input_bucket_store_;
};

}  // namespace psi::rr22
