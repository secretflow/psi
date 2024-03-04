// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/launch.h"

#include <fstream>

#include "boost/algorithm/string.hpp"
#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "google/protobuf/util/json_util.h"
#include "perfetto.h"
#include "spdlog/spdlog.h"

#include "psi/apsi/pir.h"
#include "psi/factory.h"
#include "psi/prelude.h"
#include "psi/trace_categories.h"

namespace psi {
namespace {

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

}  // namespace

PsiResultReport RunPsi(const v2::PsiConfig& psi_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  SetLogLevel(psi_config.debug_options().logging_level());

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

  std::unique_ptr<AbstractPsiParty> psi_party =
      createPsiParty(psi_config, lctx);
  PsiResultReport report = psi_party->Run();

  boost::uuids::random_generator uuid_generator;
  StopTracing(std::move(tracing_session),
              psi_config.debug_options().trace_path().empty()
                  ? fmt::format("/tmp/psi_{}.trace",
                                boost::uuids::to_string(uuid_generator()))
                  : psi_config.debug_options().trace_path());

  return report;
}

PsiResultReport RunUbPsi(const v2::UbPsiConfig& ub_psi_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
  SetLogLevel(ub_psi_config.debug_options().logging_level());

  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;

  std::string config_json;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                   ub_psi_config, &config_json, json_print_options)
                   .ok());
  SPDLOG_INFO("UB PSI config: {}", config_json);

  std::unique_ptr<AbstractUbPsiParty> ub_psi_party =
      createUbPsiParty(ub_psi_config, lctx);
  return ub_psi_party->Run();
}

PsiResultReport RunLegacyPsi(const BucketPsiConfig& bucket_psi_config,
                             const std::shared_ptr<yacl::link::Context>& lctx,
                             ProgressCallbacks progress_callbacks,
                             int64_t callbacks_interval_ms, bool ic_mode) {
  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;

  std::string config_json;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                   bucket_psi_config, &config_json, json_print_options)
                   .ok());
  SPDLOG_INFO("LEGACY PSI config: {}", config_json);

  BucketPsi bucket_psi(bucket_psi_config, lctx, ic_mode);
  return bucket_psi.Run(progress_callbacks, callbacks_interval_ms);
}

PirResultReport RunPir(const PirConfig& pir_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  YACL_ENFORCE_EQ(pir_config.pir_protocol(),
                  PirProtocol::PIR_PROTOCOL_KEYWORD_PIR_APSI);

  return apsi::Launch(pir_config, lctx);
}

}  // namespace psi
