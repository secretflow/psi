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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "yacl/link/link.h"

#include "psi/cryptor/ecc_cryptor.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

v2::RecoveryCheckpoint LoadRecoveryCheckpointFromFile(
    const std::filesystem::path& path);

// TODO(junfeng): There are some remaining work to do:
// 1. Check the integrity of cache.
// 2. Handle the situation when cache is malformed.
// Utils for recovery.
class RecoveryManager {
 public:
  // folder_path must exist.
  explicit RecoveryManager(const std::string& folder_path);

  void MarkInitEnd(const v2::PsiConfig& config,
                   const std::string& input_hash_digest);

  void MarkPreProcessEnd(const std::shared_ptr<IEccCryptor>& cryptor = nullptr);

  // Returns whether online stage is finished at this moment.
  bool MarkOnlineStart(
      const std::shared_ptr<yacl::link::Context>& lctx = nullptr);

  void UpdateEcdhDualMaskedItemSelfCount(uint64_t cnt);

  void UpdateEcdhDualMaskedItemPeerCount(uint64_t cnt);

  void UpdateParsedBucketCount(uint64_t cnt);

  void MarkOnlineEnd();

  void MarkPostProcessEnd();

  // The path of recovery file root.
  std::filesystem::path folder_path() const { return folder_path_; }

  // The path of checkpoint file which stores RecoveryCheckpoint.
  std::filesystem::path checkpoint_file_path() const {
    return checkpoint_file_path_;
  }

  // The path of private key file.
  std::filesystem::path private_key_file_path() const {
    return private_key_file_path_;
  }

  // The folder to save ECDH dual masked item from self.
  std::filesystem::path ecdh_dual_masked_self_cache_path() const {
    return ecdh_dual_masked_self_cache_path_;
  }

  // The folder to save ECDH dual masked item from peer.
  std::filesystem::path ecdh_dual_masked_peer_cache_path() const {
    return ecdh_dual_masked_peer_cache_path_;
  }

  // The folder to store KKRT and RR22 input bucket store.
  std::filesystem::path input_bucket_store_path() const {
    return input_bucket_store_path_;
  }

  uint64_t ecdh_dual_masked_cnt_from_peer() const {
    return ecdh_dual_masked_cnt_from_peer_;
  }

  uint64_t parsed_bucket_count_from_peer() const {
    return parsed_bucket_count_from_peer_;
  }

  v2::RecoveryCheckpoint checkpoint() const { return checkpoint_; }

 private:
  void SaveCheckpointFile();

  std::mutex mutex_;

  std::filesystem::path folder_path_;

  std::filesystem::path checkpoint_file_path_;

  std::filesystem::path private_key_file_path_;

  std::filesystem::path ecdh_dual_masked_self_cache_path_;

  std::filesystem::path ecdh_dual_masked_peer_cache_path_;

  std::filesystem::path input_bucket_store_path_;

  v2::RecoveryCheckpoint checkpoint_;

  uint64_t ecdh_dual_masked_cnt_from_peer_ = 0;

  uint64_t parsed_bucket_count_from_peer_ = 0;
};

}  // namespace psi
