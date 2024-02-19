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

#include "psi/utils/recovery.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "gtest/gtest.h"

#include "psi/cryptor/cryptor_selector.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

class RecoveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = "./tmp";
    std::filesystem::create_directory(tmp_dir_);
  }
  void TearDown() override {
    if (!tmp_dir_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(tmp_dir_, ec);
      // Leave error as it is, do nothing
    }
  }

  std::string tmp_dir_;
};

TEST_F(RecoveryTest, Mark) {
  RecoveryManager mgr(tmp_dir_);

  v2::PsiConfig config;
  mgr.MarkInitEnd(config, "input_hash_digest");

  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_INIT_END);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }

  std::shared_ptr<IEccCryptor> ecc_cryptor =
      CreateEccCryptor(CurveType::CURVE_25519);
  mgr.MarkPreProcessEnd(ecc_cryptor);
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_PRE_PROCESS_END);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");

    EXPECT_TRUE(std::filesystem::exists(mgr.private_key_file_path()));
  }

  mgr.MarkOnlineStart();
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_ONLINE_START);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }

  mgr.UpdateEcdhDualMaskedItemSelfCount(100);
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_ONLINE_START);
    EXPECT_EQ(pb.ecdh_dual_masked_item_self_count(), 100);
    EXPECT_EQ(pb.ecdh_dual_masked_item_peer_count(), 0);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }

  mgr.UpdateEcdhDualMaskedItemPeerCount(150);
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_ONLINE_START);
    EXPECT_EQ(pb.ecdh_dual_masked_item_self_count(), 100);
    EXPECT_EQ(pb.ecdh_dual_masked_item_peer_count(), 150);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }

  mgr.MarkOnlineEnd();
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_ONLINE_END);
    EXPECT_EQ(pb.ecdh_dual_masked_item_self_count(), 100);
    EXPECT_EQ(pb.ecdh_dual_masked_item_peer_count(), 150);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }

  mgr.MarkPostProcessEnd();
  {
    v2::RecoveryCheckpoint pb =
        LoadRecoveryCheckpointFromFile(mgr.checkpoint_file_path());

    EXPECT_EQ(pb.stage(), v2::RecoveryCheckpoint::STAGE_POST_PROCESS_END);
    EXPECT_EQ(pb.ecdh_dual_masked_item_self_count(), 100);
    EXPECT_EQ(pb.ecdh_dual_masked_item_peer_count(), 150);
    EXPECT_EQ(pb.input_hash_digest(), "input_hash_digest");
  }
}

}  // namespace psi
