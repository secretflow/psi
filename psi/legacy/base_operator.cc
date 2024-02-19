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

#include "psi/legacy/base_operator.h"

#include <utility>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/utils/serialize.h"
#include "psi/utils/sync.h"

namespace psi {

PsiBaseOperator::PsiBaseOperator(std::shared_ptr<yacl::link::Context> link_ctx)
    : link_ctx_(std::move(link_ctx)) {}

std::vector<std::string> PsiBaseOperator::Run(
    const std::vector<std::string>& inputs, bool broadcast_result) {
  auto run_f = std::async([&] { return OnRun(inputs); });
  auto res = SyncWait(link_ctx_, &run_f);

  if (broadcast_result) {
    BroadcastResult(link_ctx_, &res);
  }

  return res;
}

}  // namespace psi
