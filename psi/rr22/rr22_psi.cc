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

#include "psi/rr22/rr22_psi.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <ostream>
#include <random>

#include "sparsehash/dense_hash_map"
#include "yacl/base/byte_container_view.h"

#include "psi/rr22/okvs/galois128.h"
#include "psi/rr22/rr22_oprf.h"
#include "psi/rr22/rr22_utils.h"
#include "psi/utils/sync.h"

namespace psi::rr22 {

namespace {

size_t ComputeTruncateSize(size_t self_size, size_t peer_size, size_t ssp,
                           bool malicious) {
  size_t truncate_size =
      malicious
          ? sizeof(uint128_t)
          : std::min<size_t>(
                std::ceil((ssp + std::ceil(std::log2(self_size * peer_size))) /
                          8),
                sizeof(uint128_t));

  return truncate_size;
}

constexpr size_t kRr22OprfBinSize = 1 << 14;

}  // namespace

void Rr22PsiSenderInternal(const Rr22PsiOptions& options,
                           const std::shared_ptr<yacl::link::Context>& lctx,
                           const std::vector<uint128_t>& inputs) {
  YACL_ENFORCE(lctx->WorldSize() == 2);

  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(lctx, inputs.size());

  size_t sender_size = items_size[lctx->Rank()];
  size_t receiver_size = items_size[lctx->NextRank()];

  YACL_ENFORCE(sender_size == inputs.size());

  if ((sender_size == 0) || (receiver_size == 0)) {
    return;
  }
  YACL_ENFORCE(sender_size <= receiver_size);

  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(sender_size, receiver_size, options.ssp,
                                    options.malicious);
  }

  Rr22OprfSender oprf_sender(kRr22OprfBinSize, options.ssp, options.mode,
                             options.code_type, options.malicious);

  yacl::Buffer inputs_hash_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> inputs_hash =
      absl::MakeSpan((uint128_t*)inputs_hash_buffer.data(), inputs.size());

  oprf_sender.Send(lctx, receiver_size, absl::MakeSpan(inputs), inputs_hash,
                   options.num_threads);

  yacl::Buffer sender_oprf_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> sender_oprfs =
      absl::MakeSpan((uint128_t*)(sender_oprf_buffer.data()), inputs.size());

  SPDLOG_INFO("oprf eval begin");
  oprf_sender.Eval(inputs, inputs_hash, sender_oprfs, options.num_threads);

  SPDLOG_INFO("oprf eval end");

  // random shuffle sender's oprf values
  SPDLOG_INFO("oprf shuffle begin");
  std::mt19937 g(yacl::crypto::SecureRandU64());
  std::shuffle(sender_oprfs.begin(), sender_oprfs.end(), g);
  SPDLOG_INFO("oprf shuffle end");

  yacl::ByteContainerView oprf_byteview;
  if (options.compress) {
    uint128_t* src = sender_oprfs.data();
    uint8_t* dest = (uint8_t*)sender_oprfs.data();

    for (size_t i = 0; i < sender_oprfs.size(); ++i) {
      std::memmove(dest, src, mask_size);
      dest += mask_size;
      src += 1;
    }

    oprf_byteview = yacl::ByteContainerView(sender_oprfs.data(),
                                            sender_oprfs.size() * mask_size);
  } else {
    oprf_byteview = yacl::ByteContainerView(
        sender_oprfs.data(), sender_oprfs.size() * sizeof(uint128_t));
  }

  SPDLOG_INFO("send rr22 oprf: {} vector:{}", oprf_byteview.size(),
              sender_oprfs.size());

  for (size_t i = 0; i < sender_oprfs.size(); i += 100000) {
    size_t send_size = std::min<size_t>(100000, sender_oprfs.size() - i);
    yacl::ByteContainerView send_byteview = yacl::ByteContainerView(
        oprf_byteview.data() + i * mask_size, send_size * mask_size);

    lctx->SendAsyncThrottled(lctx->NextRank(), send_byteview,
                             fmt::format("send oprf_buf"));
  }

  SPDLOG_INFO("send rr22 oprf finished");
}

std::vector<size_t> Rr22PsiReceiverInternal(
    const Rr22PsiOptions& options,
    const std::shared_ptr<yacl::link::Context>& lctx,
    const std::vector<uint128_t>& inputs) {
  YACL_ENFORCE(lctx->WorldSize() == 2);

  // Gather Items Size
  std::vector<size_t> items_size = AllGatherItemsSize(lctx, inputs.size());

  size_t receiver_size = items_size[lctx->Rank()];
  size_t sender_size = items_size[lctx->NextRank()];

  YACL_ENFORCE(receiver_size == inputs.size());

  if ((sender_size == 0) || (receiver_size == 0)) {
    return {};
  }

  YACL_ENFORCE(sender_size <= receiver_size);

  size_t mask_size = sizeof(uint128_t);
  if (options.compress) {
    mask_size = ComputeTruncateSize(sender_size, receiver_size, options.ssp,
                                    options.malicious);
  }

  Rr22OprfReceiver oprf_receiver(kRr22OprfBinSize, options.ssp, options.mode,
                                 options.code_type, options.malicious);

  SPDLOG_INFO("out buffer begin");
  yacl::Buffer outputs_buffer(inputs.size() * sizeof(uint128_t));
  absl::Span<uint128_t> outputs =
      absl::MakeSpan((uint128_t*)(outputs_buffer.data()), inputs.size());
  SPDLOG_INFO("out buffer end");

  oprf_receiver.Recv(lctx, receiver_size, inputs, outputs, options.num_threads);

  SPDLOG_INFO("compute intersection begin, threads:{}", options.num_threads);

  std::vector<size_t> indices =
      GetIntersection(outputs, sender_size, lctx, options.num_threads,
                      options.compress, mask_size);

  SPDLOG_INFO("compute intersection end");

  return indices;
}

}  // namespace psi::rr22
