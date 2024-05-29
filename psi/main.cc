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

#include <fstream>

#include "gflags/gflags.h"
#include "google/protobuf/util/json_util.h"
#include "spdlog/spdlog.h"

#include "psi/kuscia_adapter.h"
#include "psi/launch.h"
#include "psi/version.h"

#include "psi/proto/entry.pb.h"
#include "psi/proto/pir.pb.h"
#include "psi/proto/psi.pb.h"
#include "psi/proto/psi_v2.pb.h"

DEFINE_string(config, "", "file path of launch config in JSON format.");
DEFINE_string(json, "", "config in JSON format.");
DEFINE_string(kuscia, "", "file path of kuscia task config in JSON format.");

std::string GenerateVersion() {
  return fmt::format("v{}.{}.{}{}", PSI_VERSION_MAJOR, PSI_VERSION_MINOR,
                     PSI_VERSION_PATCH, PSI_DEV_IDENTIFIER);
}

int main(int argc, char* argv[]) {
  gflags::SetVersionString(GenerateVersion());
  gflags::AllowCommandLineReparsing();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  SPDLOG_INFO("SecretFlow PSI Library {} Copyright 2023 Ant Group Co., Ltd.",
              GenerateVersion());

  psi::LaunchConfig launch_config;
  if (!FLAGS_kuscia.empty()) {
    YACL_ENFORCE(std::filesystem::exists(FLAGS_kuscia),
                 "Kuscia config file[{}] doesn't exist.", FLAGS_kuscia);
    std::fstream kuscia_config_file(FLAGS_kuscia, std::ios::in);
    psi::KusciaTask kuscia_config = psi::FromKusciaConfig(
        std::string((std::istreambuf_iterator<char>(kuscia_config_file)),
                    std::istreambuf_iterator<char>()));

    SPDLOG_INFO("Kuscia task id: {}", kuscia_config.task_id);
    launch_config = kuscia_config.launch_config;
  } else if (!FLAGS_json.empty()) {
    google::protobuf::util::JsonParseOptions json_parse_options;
    json_parse_options.ignore_unknown_fields = false;  // optional
    auto status = google::protobuf::util::JsonStringToMessage(
        FLAGS_json, &launch_config, json_parse_options);

    YACL_ENFORCE(status.ok(),
                 "Launch config JSON string couldn't be parsed: {}",
                 FLAGS_json);
  } else {
    YACL_ENFORCE(std::filesystem::exists(FLAGS_config),
                 "Config file[{}] doesn't exist.", FLAGS_config);
    std::fstream json_config_file(FLAGS_config, std::ios::in);
    std::string config_json((std::istreambuf_iterator<char>(json_config_file)),
                            std::istreambuf_iterator<char>());

    google::protobuf::util::JsonParseOptions json_parse_options;
    json_parse_options.ignore_unknown_fields = false;  // optional
    auto status = google::protobuf::util::JsonStringToMessage(
        config_json, &launch_config, json_parse_options);

    YACL_ENFORCE(status.ok(),
                 "Launch config JSON string couldn't be parsed: {}",
                 config_json);
  }

  std::shared_ptr<yacl::link::Context> lctx = nullptr;
  if (!launch_config.self_link_party().empty()) {
    yacl::link::ContextDesc lctx_desc(launch_config.link_config());
    int rank = -1;
    for (int i = 0; i < launch_config.link_config().parties().size(); i++) {
      if (launch_config.link_config().parties(i).id() ==
          launch_config.self_link_party()) {
        rank = i;
      }
    }
    YACL_ENFORCE_GE(rank, 0, "Couldn't find rank in YACL Link.");
    lctx = yacl::link::FactoryBrpc().CreateContext(lctx_desc, rank);
  }

  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;
  std::string report_json;
  if (launch_config.has_legacy_psi_config()) {
    psi::PsiResultReport report =
        psi::RunLegacyPsi(launch_config.legacy_psi_config(), lctx);
    YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                     report, &report_json, json_print_options)
                     .ok());
  } else if (launch_config.has_psi_config()) {
    psi::PsiResultReport report = psi::RunPsi(launch_config.psi_config(), lctx);
    YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                     report, &report_json, json_print_options)
                     .ok());
  } else if (launch_config.has_ub_psi_config()) {
    psi::PsiResultReport report =
        psi::RunUbPsi(launch_config.ub_psi_config(), lctx);
    YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                     report, &report_json, json_print_options)
                     .ok());
  } else if (launch_config.has_pir_config()) {
    psi::PirResultReport report = psi::RunPir(launch_config.pir_config(), lctx);
    YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                     report, &report_json, json_print_options)
                     .ok());
  } else {
    SPDLOG_WARN("No runtime config is provided.");
  }

  SPDLOG_INFO("Report: {}", report_json);
  SPDLOG_INFO("Thank you for trusting SecretFlow PSI Library.");
  return 0;
}
