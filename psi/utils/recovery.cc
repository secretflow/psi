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

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"
#include "yacl/base/exception.h"

#include "psi/utils/io.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

v2::RecoveryCheckpoint LoadRecoveryCheckpointFromFile(
    const std::filesystem::path& path) {
  v2::RecoveryCheckpoint pb;
  google::protobuf::util::JsonParseOptions json_parse_options;
  json_parse_options.ignore_unknown_fields = true;  // optional
  std::fstream json_config_file(path, std::ios::in);

  auto status = google::protobuf::util::JsonStringToMessage(
      std::string((std::istreambuf_iterator<char>(json_config_file)),
                  std::istreambuf_iterator<char>()),
      &pb, json_parse_options);

  YACL_ENFORCE(status.ok(),
               "file {} couldn't be parsed as RecoveryCheckpoint: {}.",
               path.string(), status.ToString());

  return pb;
}

RecoveryManager::RecoveryManager(const std::string& folder_path)
    : folder_path_(folder_path) {
  YACL_ENFORCE(std::filesystem::exists(folder_path_));

  checkpoint_file_path_ = folder_path_ / "checkpoint.json";
  private_key_file_path_ = folder_path_ / "private_key";

  ecdh_dual_masked_self_cache_path_ =
      folder_path_ / "ecdh_dual_masked_self_cache";
  if (!std::filesystem::exists(ecdh_dual_masked_self_cache_path_)) {
    std::filesystem::create_directory(ecdh_dual_masked_self_cache_path_);
  }

  ecdh_dual_masked_peer_cache_path_ =
      folder_path_ / "ecdh_dual_masked_peer_cache";
  if (!std::filesystem::exists(ecdh_dual_masked_peer_cache_path_)) {
    std::filesystem::create_directory(ecdh_dual_masked_peer_cache_path_);
  }

  input_bucket_store_path_ = folder_path_ / "input_bucket_store";
  if (!std::filesystem::exists(input_bucket_store_path_)) {
    std::filesystem::create_directory(input_bucket_store_path_);
  }

  // If checkpoint_file_path_ exists, load previous record for recovery.
  if (std::filesystem::exists(checkpoint_file_path_)) {
    checkpoint_ = LoadRecoveryCheckpointFromFile(checkpoint_file_path_);
  }
}

void RecoveryManager::SaveCheckpointFile() {
  std::unique_lock<std::mutex> lock(mutex_);

  std::string checkpoint_json;
  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                   checkpoint_, &checkpoint_json, json_print_options)
                   .ok());

  std::ofstream file;
  file.open(checkpoint_file_path_);
  file << checkpoint_json;
  file.close();
}

void RecoveryManager::MarkInitEnd(const v2::PsiConfig& config,
                                  const std::string& input_hash_digest) {
  if (checkpoint_.stage() != v2::RecoveryCheckpoint::STAGE_UNSPECIFIED) {
    // Check whether config and input_hash_digest match the previous record.
    YACL_ENFORCE(::google::protobuf::util::MessageDifferencer::Equals(
                     config, checkpoint_.config()),
                 "PSI Config doesn't match previous record.");

    YACL_ENFORCE(input_hash_digest == checkpoint_.input_hash_digest(),
                 "input_hash_digest doesn't match previous record.");
  } else {
    checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_INIT_END);
    *checkpoint_.mutable_config() = config;
    checkpoint_.set_input_hash_digest(input_hash_digest);

    SaveCheckpointFile();
  }
}

void RecoveryManager::MarkPreProcessEnd(
    const std::shared_ptr<IEccCryptor>& cryptor) {
  if (cryptor) {
    if (checkpoint_.stage() < v2::RecoveryCheckpoint::STAGE_PRE_PROCESS_END) {
      checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_PRE_PROCESS_END);

      // save private key
      auto private_key_output =
          io::BuildOutputStream(io::FileIoOptions(private_key_file_path_));
      private_key_output->Write(cryptor->GetPrivateKey(), kEccKeySize);
      private_key_output->Flush();

      // save checkpoint file
      SaveCheckpointFile();
    } else {
      // Load private key.
      std::ifstream instream(private_key_file_path_,
                             std::ios::in | std::ios::binary);
      std::vector<uint8_t> private_key(
          (std::istreambuf_iterator<char>(instream)),
          std::istreambuf_iterator<char>());
      cryptor->SetPrivateKey(private_key);
    }
  }
}

bool RecoveryManager::MarkOnlineStart(
    const std::shared_ptr<yacl::link::Context>& lctx) {
  bool online_stage_finished = false;
  if (lctx) {
    YACL_ENFORCE_EQ(lctx->WorldSize(), 2U);

    v2::InternalRecoveryRecord record;

    record.set_stage(checkpoint_.stage());
    record.set_ecdh_dual_masked_item_peer_count(
        checkpoint_.ecdh_dual_masked_item_peer_count());
    record.set_parsed_bucket_count(checkpoint_.parsed_bucket_count());
    std::string record_str;
    YACL_ENFORCE(record.SerializeToString(&record_str));

    std::vector<yacl::Buffer> record_buffer_list = yacl::link::AllGather(
        lctx, record_str, "RecoveryManager::MarkOnlineStart");

    const std::string rank0_serialized(record_buffer_list[0].data<char>(),
                                       record_buffer_list[0].size());

    const std::string rank1_serialized(record_buffer_list[1].data<char>(),
                                       record_buffer_list[1].size());

    v2::InternalRecoveryRecord rank0;
    YACL_ENFORCE(rank0.ParseFromString(rank0_serialized));

    v2::InternalRecoveryRecord rank1;
    YACL_ENFORCE(rank1.ParseFromString(rank1_serialized));

    if (rank0.stage() >= v2::RecoveryCheckpoint::STAGE_ONLINE_END &&
        rank1.stage() >= v2::RecoveryCheckpoint::STAGE_ONLINE_END) {
      online_stage_finished = true;
    }

    if (lctx->Rank() == 0) {
      ecdh_dual_masked_cnt_from_peer_ =
          rank1.ecdh_dual_masked_item_peer_count();
      parsed_bucket_count_from_peer_ = rank1.parsed_bucket_count();
    } else {
      ecdh_dual_masked_cnt_from_peer_ =
          rank0.ecdh_dual_masked_item_peer_count();
      parsed_bucket_count_from_peer_ = rank0.parsed_bucket_count();
    }
  }

  SPDLOG_INFO(
      "RecoveryManager::MarkOnlineStart ecdh_dual_masked_cnt_from_peer_ = {}",
      ecdh_dual_masked_cnt_from_peer_);

  SPDLOG_INFO(
      "RecoveryManager::MarkOnlineStart parsed_bucket_count_from_peer_ = "
      "{}",
      parsed_bucket_count_from_peer_);

  if (checkpoint_.stage() < v2::RecoveryCheckpoint::STAGE_ONLINE_START) {
    checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_ONLINE_START);

    // save checkpoint file
    SaveCheckpointFile();
  }

  return online_stage_finished;
}

void RecoveryManager::UpdateEcdhDualMaskedItemSelfCount(uint64_t cnt) {
  checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_ONLINE_START);
  checkpoint_.set_ecdh_dual_masked_item_self_count(cnt);

  // save checkpoint file
  SaveCheckpointFile();
}

void RecoveryManager::UpdateEcdhDualMaskedItemPeerCount(uint64_t cnt) {
  SPDLOG_INFO("RecoveryManager::UpdateEcdhDualMaskedItemPeerCount, cnt = {}",
              cnt);
  checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_ONLINE_START);
  checkpoint_.set_ecdh_dual_masked_item_peer_count(cnt);

  // save checkpoint file
  SaveCheckpointFile();
}

void RecoveryManager::UpdateParsedBucketCount(uint64_t cnt) {
  SPDLOG_INFO("RecoveryManager::UpdateParsedBucketCount, cnt = {}", cnt);
  checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_ONLINE_START);
  checkpoint_.set_parsed_bucket_count(cnt);

  // save checkpoint file
  SaveCheckpointFile();
}

void RecoveryManager::MarkOnlineEnd() {
  if (checkpoint_.stage() < v2::RecoveryCheckpoint::STAGE_ONLINE_END) {
    checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_ONLINE_END);

    // save checkpoint file
    SaveCheckpointFile();
  }
}

void RecoveryManager::MarkPostProcessEnd() {
  if (checkpoint_.stage() < v2::RecoveryCheckpoint::STAGE_POST_PROCESS_END) {
    checkpoint_.set_stage(v2::RecoveryCheckpoint::STAGE_POST_PROCESS_END);

    // save checkpoint file
    SaveCheckpointFile();
  }
}

}  // namespace psi