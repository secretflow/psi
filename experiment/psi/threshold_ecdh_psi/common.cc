// Copyright 2025
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

#include "experiment/psi/threshold_ecdh_psi/common.h"

#include "spdlog/spdlog.h"

#include "psi/utils/io.h"
#include "psi/utils/test_utils.h"

#include "psi/proto/psi.pb.h"

namespace psi::ecdh {
void SaveIntersectionCount(const std::string& count_path, uint32_t real_count,
                           uint32_t final_count) {
  auto ofs = io::BuildOutputStream(io::FileIoOptions(count_path));

  ofs->Write("real_count,final_count\n");
  ofs->Write(fmt::format("{},{}\n", real_count, final_count));

  ofs->Close();
  SPDLOG_INFO("Save intersection count to {}", count_path);
}

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

  test::WriteCsvFile(sender_input_path, header, items_sender);
  test::WriteCsvFile(receiver_input_path, header, items_receiver);

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
}  // namespace psi::ecdh