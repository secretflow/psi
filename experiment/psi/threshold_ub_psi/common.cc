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

#include "experiment/psi/threshold_ub_psi/common.h"

#include "spdlog/spdlog.h"

#include "psi/utils/io.h"
#include "psi/utils/test_utils.h"

namespace psi::ecdh {

void SaveIntersectCount(const std::string &count_path, uint32_t real_count,
                        uint32_t final_count) {
  auto ofs = io::BuildOutputStream(io::FileIoOptions(count_path));

  ofs->Write("real_count,final_count\n");
  ofs->Write(fmt::format("{},{}\n", real_count, final_count));

  ofs->Close();
  SPDLOG_INFO("Save intersect count to {}", count_path);
}

void GenerateUbPsiConfig(const std::filesystem::path &tmp_folder,
                         const std::vector<std::string> &items_server,
                         const std::vector<std::string> &items_client,
                         uint32_t threshold, v2::UbPsiConfig &server_config,
                         v2::UbPsiConfig &client_config) {
  std::string server_input_path = tmp_folder / "server_input.csv";
  std::string client_input_path = tmp_folder / "client_input.csv";
  std::string server_output_path = tmp_folder / "server_output.csv";
  std::string client_output_path = tmp_folder / "client_output.csv";
  std::string server_cache_path = tmp_folder / "server_cache";
  std::string client_cache_path = tmp_folder / "client_cache";

  test::WriteCsvFile(server_input_path, items_server);
  test::WriteCsvFile(client_input_path, items_client);

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
}  // namespace psi::ecdh