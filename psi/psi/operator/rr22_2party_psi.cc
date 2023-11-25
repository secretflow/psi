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

#include "psi/psi/operator/rr22_2party_psi.h"

#include <chrono>

#include "omp.h"
#include "yacl/crypto/base/hash/hash_utils.h"
#include "yacl/utils/parallel.h"

#include "psi/psi/operator/factory.h"

using DurationMillis = std::chrono::duration<double, std::milli>;

namespace psi::psi {

Rr22PsiOperator::Options Rr22PsiOperator::ParseConfig(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  Options options;
  options.link_ctx = lctx;
  options.receiver_rank = config.receiver_rank();

  size_t thread_num = omp_get_num_procs();

  options.rr22_options.ssp = 40;
  options.rr22_options.num_threads = thread_num;
  options.rr22_options.compress = true;

  return options;
}

std::vector<std::string> Rr22PsiOperator::OnRun(
    const std::vector<std::string>& inputs) {
  std::vector<std::string> result;

  // hash items to uint128_t
  std::vector<uint128_t> items_hash(inputs.size());
  SPDLOG_INFO("begin items hash");
  yacl::parallel_for(0, inputs.size(), 1, [&](int64_t begin, int64_t end) {
    for (int64_t idx = begin; idx < end; ++idx) {
      items_hash[idx] = yacl::crypto::Blake3_128(inputs[idx]);
    }
  });
  SPDLOG_INFO("end items hash");

  const auto psi_core_start = std::chrono::system_clock::now();

  if (options_.receiver_rank == link_ctx_->Rank()) {
    std::vector<size_t> rr22_psi_result =
        Rr22PsiReceiver(options_.rr22_options, options_.link_ctx, items_hash);

    const auto psi_core_end = std::chrono::system_clock::now();
    const DurationMillis psi_core_duration = psi_core_end - psi_core_start;
    SPDLOG_INFO("rank: {}, psi_core_duration:{}", options_.link_ctx->Rank(),
                (psi_core_duration.count() / 1000));

    result.reserve(rr22_psi_result.size());

    for (auto index : rr22_psi_result) {
      result.push_back(inputs[index]);
    }
  } else {
    Rr22PsiSender(options_.rr22_options, options_.link_ctx, items_hash);

    const auto psi_core_end = std::chrono::system_clock::now();
    const DurationMillis psi_core_duration = psi_core_end - psi_core_start;
    SPDLOG_INFO("rank: {}, psi_core_duration:{}", options_.link_ctx->Rank(),
                (psi_core_duration.count() / 1000));
  }

  return result;
}

namespace {

std::unique_ptr<PsiBaseOperator> CreateFastOperator(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto options = Rr22PsiOperator::ParseConfig(config, lctx);

  return std::make_unique<Rr22PsiOperator>(options);
}

std::unique_ptr<PsiBaseOperator> CreateLowCommOperator(
    const MemoryPsiConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto options = Rr22PsiOperator::ParseConfig(config, lctx);

  options.rr22_options.mode = Rr22PsiMode::LowCommMode;

  return std::make_unique<Rr22PsiOperator>(options);
}

REGISTER_OPERATOR(RR22_FAST_PSI_2PC, CreateFastOperator);
REGISTER_OPERATOR(RR22_LOWCOMM_PSI_2PC, CreateLowCommOperator);

}  // namespace

}  // namespace psi::psi
