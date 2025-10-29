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

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yacl/crypto/hash/hash_utils.h"

#include "psi/utils/io.h"

#include "psi/proto/psi.pb.h"
#include "psi/proto/psi_v2.pb.h"

namespace psi::test {

inline std::vector<std::string> CreateRangeItems(size_t begin, size_t size) {
  std::vector<std::string> ret;
  for (size_t i = 0; i < size; i++) {
    ret.push_back(std::to_string(begin + i));
  }
  return ret;
}

inline std::vector<std::string> GetIntersection(
    const std::vector<std::string>& items_a,
    const std::vector<std::string>& items_b) {
  std::set<std::string> set(items_a.begin(), items_a.end());
  std::vector<std::string> ret;
  for (const auto& s : items_b) {
    if (set.count(s) != 0) {
      ret.push_back(s);
    }
  }
  return ret;
}

inline std::vector<uint128_t> CreateItemHashes(size_t begin, size_t size) {
  std::vector<uint128_t> ret;
  for (size_t i = 0; i < size; i++) {
    ret.push_back(yacl::crypto::Blake3_128(std::to_string(begin + i)));
  }
  return ret;
}

inline std::optional<CurveType> GetOverrideCurveType() {
  if (const auto* env = std::getenv("OVERRIDE_CURVE")) {
    if (std::strcmp(env, "25519") == 0) {
      return CurveType::CURVE_25519;
    }
    if (std::strcmp(env, "FOURQ") == 0) {
      return CurveType::CURVE_FOURQ;
    }
  }
  return {};
}

inline void WriteCsvFile(const std::string& file_path,
                         const std::string& header,
                         const std::vector<std::string>& items) {
  auto out = io::BuildOutputStream(io::FileIoOptions(file_path));
  out->Write(fmt::format("{}\n", header));
  for (const auto& data : items) {
    out->Write(fmt::format("{}\n", data));
  }
  out->Close();
}

// Generate UbPsiConfig. This method is only used for threshold ub psi test and
// benchmark.
void GenerateUbPsiConfig(const std::filesystem::path& tmp_folder,
                         const std::vector<std::string>& items_server,
                         const std::vector<std::string>& items_client,
                         uint32_t threshold, v2::UbPsiConfig& server_config,
                         v2::UbPsiConfig& client_config) {
  std::string server_input_path = tmp_folder / "server_input.csv";
  std::string client_input_path = tmp_folder / "client_input.csv";
  std::string server_output_path = tmp_folder / "server_output.csv";
  std::string client_output_path = tmp_folder / "client_output.csv";
  std::string server_cache_path = tmp_folder / "server_cache";
  std::string client_cache_path = tmp_folder / "client_cache";
  std::string header = "id";

  WriteCsvFile(server_input_path, header, items_server);
  WriteCsvFile(client_input_path, header, items_client);

  server_config.set_mode(v2::UbPsiConfig::MODE_FULL);
  server_config.set_role(v2::Role::ROLE_SERVER);
  server_config.set_cache_path(server_cache_path);
  server_config.mutable_input_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  server_config.mutable_input_config()->set_path(server_input_path);
  server_config.mutable_output_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  server_config.mutable_output_config()->set_path(server_output_path);
  server_config.add_keys("id");
  server_config.set_intersection_threshold(threshold);
  server_config.set_server_get_result(true);
  server_config.set_client_get_result(true);

  client_config.set_mode(v2::UbPsiConfig::MODE_FULL);
  client_config.set_role(v2::Role::ROLE_CLIENT);
  client_config.set_cache_path(client_cache_path);
  client_config.mutable_input_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  client_config.mutable_input_config()->set_path(client_input_path);
  client_config.mutable_output_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  client_config.mutable_output_config()->set_path(client_output_path);
  client_config.add_keys("id");
  client_config.set_intersection_threshold(threshold);
  client_config.set_server_get_result(true);
  client_config.set_client_get_result(true);
}

// Generate PsiConfig. This method is only used for threshold ecdh psi test and
// benchmark.
void GeneratePsiConfig(const std::filesystem::path& tmp_folder,
                       const std::vector<std::string>& items_sender,
                       const std::vector<std::string>& items_receiver,
                       uint32_t threshold, v2::PsiConfig& sender_config,
                       v2::PsiConfig& receiver_config) {
  std::string sender_input_path = tmp_folder / "sender_input.csv";
  std::string receiver_input_path = tmp_folder / "receiver_input.csv";
  std::string sender_output_path = tmp_folder / "sender_output.csv";
  std::string receiver_output_path = tmp_folder / "receiver_output.csv";
  std::string header = "id";

  WriteCsvFile(sender_input_path, header, items_sender);
  WriteCsvFile(receiver_input_path, header, items_receiver);

  sender_config.mutable_protocol_config()->set_protocol(
      v2::Protocol::PROTOCOL_ECDH);
  sender_config.mutable_protocol_config()->mutable_ecdh_config()->set_curve(
      CurveType::CURVE_FOURQ);
  sender_config.mutable_protocol_config()->set_role(v2::Role::ROLE_SENDER);
  sender_config.mutable_protocol_config()->set_broadcast_result(true);
  sender_config.mutable_input_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  sender_config.mutable_input_config()->set_path(sender_input_path);
  sender_config.mutable_output_config()->set_type(v2::IoType::IO_TYPE_FILE_CSV);
  sender_config.mutable_output_config()->set_path(sender_output_path);
  sender_config.add_keys("id");
  sender_config.set_intersection_threshold(threshold);

  receiver_config.mutable_protocol_config()->set_protocol(
      v2::Protocol::PROTOCOL_ECDH);
  receiver_config.mutable_protocol_config()->mutable_ecdh_config()->set_curve(
      CurveType::CURVE_FOURQ);
  receiver_config.mutable_protocol_config()->set_role(v2::Role::ROLE_RECEIVER);
  receiver_config.mutable_protocol_config()->set_broadcast_result(true);
  receiver_config.mutable_input_config()->set_type(
      v2::IoType::IO_TYPE_FILE_CSV);
  receiver_config.mutable_input_config()->set_path(receiver_input_path);
  receiver_config.mutable_output_config()->set_type(
      v2::IoType::IO_TYPE_FILE_CSV);
  receiver_config.mutable_output_config()->set_path(receiver_output_path);
  receiver_config.add_keys("id");
  receiver_config.set_intersection_threshold(threshold);
}

}  // namespace psi::test
