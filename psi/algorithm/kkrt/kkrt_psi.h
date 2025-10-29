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

#pragma once

#include <memory>
#include <vector>

#include "yacl/kernel/type/ot_store.h"
#include "yacl/link/link.h"

#include "psi/algorithm/psi_io.h"

//
// implementation of KKRT16 PSI protocol
// https://eprint.iacr.org/2016/799.pdf
//
// use Stash less Cuckoo hash optimization
// Reference:
// PSZ18 Scalable private set intersection based on ot extension
// https://eprint.iacr.org/2016/930.pdf
//
namespace psi::kkrt {

struct KkrtPsiOptions {
  // batch size the receiver send corrections
  size_t ot_batch_size = 128;

  // batch size the sender used to send oprf encode
  size_t psi_batch_size = 128;

  // cuckoo hash parameter
  // now use stashless setting
  // stash_size = 0  cuckoo_hash_num =3
  // use stat_sec_param = 40
  size_t cuckoo_hash_num = 3;
  size_t stash_size = 0;
  size_t stat_sec_param = 40;
};

yacl::crypto::OtRecvStore GetKkrtOtSenderOptions(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t num_ot);

yacl::crypto::OtSendStore GetKkrtOtReceiverOptions(
    const std::shared_ptr<yacl::link::Context>& link_ctx, size_t num_ot);

KkrtPsiOptions GetDefaultKkrtPsiOptions();

//
// sender and receiver psi input data shoud be prepocessed using hash algorithm.
// like sha256 or blake2/blake3 hash algorithm or aes_ecb(key, x)^x
//

std::vector<PsiResultIndex> KkrtPsiRecvIndices(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    const KkrtPsiOptions& kkrt_psi_options,
    const yacl::crypto::OtSendStore& ot_send,
    const std::vector<uint128_t>& items_hash);

void KkrtPsiSend(const std::shared_ptr<yacl::link::Context>& link_ctx,
                 const KkrtPsiOptions& kkrt_psi_options,
                 const yacl::crypto::OtRecvStore& ot_recv,
                 const std::vector<PsiItemHash>& items);

// pair first: item index,
// pair second: item extra duplication count (0: item unique)
inline std::pair<std::vector<size_t>, std::vector<uint32_t>> KkrtPsiRecv(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    const KkrtPsiOptions& kkrt_psi_options,  // with kkrt options
    const yacl::crypto::OtSendStore& ot_send,
    const std::vector<uint128_t>& items_hash) {
  auto result_indices =
      KkrtPsiRecvIndices(link_ctx, kkrt_psi_options, ot_send, items_hash);

  std::vector<size_t> item_indices;
  item_indices.reserve(result_indices.size());
  std::vector<uint32_t> item_extra_dup_cnts(result_indices.size(), 0);

  for (size_t i = 0; i != result_indices.size(); ++i) {
    item_indices.emplace_back(result_indices[i].data);
    if (result_indices[i].peer_item_cnt > 1) {
      item_extra_dup_cnts[i] = result_indices[i].peer_item_cnt - 1;
    }
  }

  return {item_indices, item_extra_dup_cnts};
}

inline void KkrtPsiSend(const std::shared_ptr<yacl::link::Context>& link_ctx,
                        const yacl::crypto::OtRecvStore& ot_recv,
                        const std::vector<PsiItemHash>& items) {
  KkrtPsiOptions kkrt_psi_options = GetDefaultKkrtPsiOptions();
  KkrtPsiSend(link_ctx, kkrt_psi_options, ot_recv, items);
}

inline std::pair<std::vector<size_t>, std::vector<uint32_t>> KkrtPsiRecv(
    const std::shared_ptr<yacl::link::Context>& link_ctx,
    const yacl::crypto::OtSendStore& ot_send,
    const std::vector<uint128_t>& items_hash) {
  KkrtPsiOptions kkrt_psi_options = GetDefaultKkrtPsiOptions();
  return KkrtPsiRecv(link_ctx, kkrt_psi_options, ot_send, items_hash);
}

inline void KkrtPsiSend(const std::shared_ptr<yacl::link::Context>& link_ctx,
                        const yacl::crypto::OtRecvStore& ot_recv,
                        const std::vector<uint128_t>& items_hash) {
  std::vector<PsiItemHash> items;
  items.reserve(items_hash.size());
  for (const auto& item : items_hash) {
    PsiItemHash hash;
    hash.data = item;
    items.emplace_back(std::move(hash));
  }
  KkrtPsiSend(link_ctx, ot_recv, items);
}

}  // namespace psi::kkrt
