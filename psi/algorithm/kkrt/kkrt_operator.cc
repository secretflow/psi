// Copyright 2025 Ant Group Co., Ltd.
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

#include "psi/algorithm/kkrt/kkrt_operator.h"

#include "yacl/crypto/hash/hash_utils.h"

#include "psi/algorithm/kkrt/common.h"

namespace psi::kkrt {

KkrtOperator::KkrtOperator(Options opts,
                           std::shared_ptr<IDataStore> input_store,
                           std::shared_ptr<IResultStore> output_store)
    : PsiOperator(opts.link_ctx, std::move(input_store),
                  std::move(output_store), nullptr, opts.broadcast_result),
      opts_(std::move(opts)),
      is_recevicer_(opts_.receiver_rank == link_ctx_->Rank()) {}

bool KkrtOperator::ReceiveResult() {
  return is_recevicer_ || opts_.broadcast_result;
}

void KkrtOperator::OnInit() {
  if (is_recevicer_) {
    ot_send_ = std::make_unique<yacl::crypto::OtSendStore>(
        GetKkrtOtReceiverOptions(link_ctx_, kDefaultNumOt));
  } else {
    ot_recv_ = std::make_unique<yacl::crypto::OtRecvStore>(
        GetKkrtOtSenderOptions(link_ctx_, kDefaultNumOt));
  }
}

std::vector<PsiResultIndex> KkrtOperator::OnRun(
    const std::vector<PsiItemData>& inputs) {
  std::vector<PsiResultIndex> intersections;

  if (is_recevicer_) {
    std::vector<uint128_t> items_hash(inputs.size());
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t i = begin; i < end; ++i) {
        items_hash[i] = yacl::crypto::Blake3_128(inputs[i].buf);
      }
    });

    intersections =
        KkrtPsiRecvIndices(link_ctx_, opts_.psi_opts, *ot_send_, items_hash);
  } else {
    std::vector<PsiItemHash> items_hash(inputs.size());
    yacl::parallel_for(0, inputs.size(), [&](int64_t begin, int64_t end) {
      for (int64_t i = begin; i < end; ++i) {
        items_hash[i].data = yacl::crypto::Blake3_128(inputs[i].buf);
        items_hash[i].cnt = inputs[i].cnt;
      }
    });

    KkrtPsiSend(link_ctx_, opts_.psi_opts, *ot_recv_, items_hash);
  }

  return intersections;
}

void KkrtOperator::OnEnd() {}

}  // namespace psi::kkrt
