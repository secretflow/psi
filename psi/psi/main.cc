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

// perfetto usage is adapted from
// https://github.com/google/perfetto/blob/master/examples/sdk/example.cc

#include <spdlog/spdlog.h>

#include <cctype>
#include <fstream>
#include <iostream>

#include "boost/algorithm/string.hpp"
#include "gflags/gflags.h"
#include "google/protobuf/util/json_util.h"
#include "perfetto.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/psi/factory.h"
#include "psi/psi/kuscia_adapter.h"
#include "psi/psi/trace_categories.h"
#include "psi/version.h"

#include "psi/proto/psi.pb.h"

DEFINE_string(config, "", "file path of psi config in JSON format.");
DEFINE_string(kuscia, "", "file path of kuscia task config in JSON format.");

void InitializePerfetto() {
  perfetto::TracingInitArgs args;

  // TODO(junfeng): include system backend as well.
  args.backends = perfetto::kInProcessBackend;

  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();
}

std::unique_ptr<perfetto::TracingSession> StartTracing() {
  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  auto tracing_session = perfetto::Tracing::NewTrace();
  tracing_session->Setup(cfg);
  tracing_session->StartBlocking();
  return tracing_session;
}

void StopTracing(std::unique_ptr<perfetto::TracingSession> tracing_session,
                 const std::string& path) {
  perfetto::TrackEvent::Flush();

  // Stop tracing and read the trace data.
  tracing_session->StopBlocking();
  std::vector<char> trace_data(tracing_session->ReadTraceBlocking());

  // Write the result into a file.
  std::ofstream output;
  output.open(path, std::ios::out | std::ios::binary);
  output.write(&trace_data[0], std::streamsize(trace_data.size()));
  output.close();
  SPDLOG_INFO("Trace has been written to {}.", path);
}

void SetLogLevel(const std::string& level) {
  std::string normalized_level = boost::algorithm::to_lower_copy(level);

  if (normalized_level == "trace") {
    spdlog::set_level(spdlog::level::trace);
  } else if (normalized_level == "debug") {
    spdlog::set_level(spdlog::level::debug);
  } else if (normalized_level == "info" || normalized_level.empty()) {
    spdlog::set_level(spdlog::level::info);
  } else if (normalized_level == "warn") {
    spdlog::set_level(spdlog::level::warn);
  } else if (normalized_level == "err") {
    spdlog::set_level(spdlog::level::err);
  } else if (normalized_level == "critical") {
    spdlog::set_level(spdlog::level::critical);
  } else if (normalized_level == "off") {
    spdlog::set_level(spdlog::level::off);
  } else {
    YACL_THROW("unsupported logging level: {}", level);
  }
}

std::string GenerateVersion() {
  return fmt::format("v{}.{}.{}{}", PSI_VERSION_MAJOR, PSI_VERSION_MINOR,
                     PSI_VERSION_PATCH, PSI_DEV_IDENTIFIER);
}

int main(int argc, char* argv[]) {
  gflags::SetVersionString(GenerateVersion());
  gflags::AllowCommandLineReparsing();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  psi::psi::v2::PsiConfig psi_config;

  if (!FLAGS_kuscia.empty()) {
    std::fstream json_config_file(FLAGS_kuscia, std::ios::in);
    psi_config =
        psi::psi::FromKusciaConfig(
            std::string((std::istreambuf_iterator<char>(json_config_file)),
                        std::istreambuf_iterator<char>()))
            .psi_config;
  } else {
    google::protobuf::util::JsonParseOptions json_parse_options;
    json_parse_options.ignore_unknown_fields = false;  // optional
    std::fstream json_config_file(FLAGS_config, std::ios::in);

    auto status = google::protobuf::util::JsonStringToMessage(
        std::string((std::istreambuf_iterator<char>(json_config_file)),
                    std::istreambuf_iterator<char>()),
        &psi_config, json_parse_options);

    YACL_ENFORCE(status.ok(), "file {} couldn't be parsed as PsiConfig: {}.",
                 FLAGS_config, status.ToString());
  }

  SetLogLevel(psi_config.debug_options().logging_level());

  SPDLOG_INFO("SecretFlow PSI Library {} Copyright 2023 Ant Group Co., Ltd.",
              GenerateVersion());

  InitializePerfetto();
  auto tracing_session = StartTracing();
  // Give a custom name for the traced process.
  perfetto::ProcessTrack process_track = perfetto::ProcessTrack::Current();
  perfetto::protos::gen::TrackDescriptor desc = process_track.Serialize();
  desc.mutable_process()->set_process_name("psi");
  perfetto::TrackEvent::SetTrackDescriptor(process_track, desc);

  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;

  std::string config_json;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                   psi_config, &config_json, json_print_options)
                   .ok());
  SPDLOG_INFO("PSI config: {}", config_json);

  std::unique_ptr<psi::psi::AbstractPSIParty> psi_party =
      psi::psi::createPSIParty(psi_config);
  psi::psi::v2::PsiReport report = psi_party->Run();

  StopTracing(std::move(tracing_session),
              psi_config.debug_options().trace_path().empty()
                  ? fmt::format("/tmp/psi_{}.trace",
                                std::to_string(absl::ToUnixNanos(absl::Now())))
                  : psi_config.debug_options().trace_path());

  std::string report_json;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(report, &report_json,
                                                           json_print_options)
                   .ok());
  SPDLOG_INFO("PSI report: {}", report_json);
  SPDLOG_INFO("Thank you for trusting SecretFlow PSI Library.");

  return 0;
}
