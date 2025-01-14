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

#include "psi/apps/psi_launcher/launch.h"

#include <fstream>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/util/json_util.h"
#include "perfetto.h"
#include "spdlog/spdlog.h"

#include "psi/apps/psi_launcher/factory.h"
#include "psi/prelude.h"
#include "psi/trace_categories.h"
#include "psi/utils/random_str.h"
#include "psi/wrapper/apsi/cli/entry.h"

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
  static const std::map<std::string, spdlog::level::level_enum> kLogLevelMap = {
      {"trace", spdlog::level::trace}, {"debug", spdlog::level::debug},
      {"info", spdlog::level::info},   {"warn", spdlog::level::warn},
      {"err", spdlog::level::err},     {"critical", spdlog::level::critical},
      {"off", spdlog::level::off}};
  static const std::string kDefaultLogLevel = "info";

  std::string normalized_level = boost::algorithm::to_lower_copy(level);
  if (normalized_level.empty()) {
    normalized_level = kDefaultLogLevel;
  }

  auto level_iter = kLogLevelMap.find(normalized_level);
  YACL_ENFORCE(level_iter != kLogLevelMap.end(),
               "unsupported logging level: {}", level);
  spdlog::set_level(level_iter->second);
  spdlog::flush_on(level_iter->second);
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

  StopTracing(std::move(tracing_session),
              psi_config.debug_options().trace_path().empty()
                  ? fmt::format("/tmp/psi_{}.trace", GetRandomString())
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

PirResultReport RunPir(const ApsiReceiverConfig& apsi_receiver_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::apsi_wrapper::cli::ReceiverOptions options;
  options.threads = apsi_receiver_config.threads();
  if (apsi_receiver_config.log_level() == "all" ||
      apsi_receiver_config.log_level() == "debug" ||
      apsi_receiver_config.log_level() == "info" ||
      apsi_receiver_config.log_level() == "warning" ||
      apsi_receiver_config.log_level() == "error" ||
      apsi_receiver_config.log_level() == "off") {
    options.log_level = apsi_receiver_config.log_level();
  }
  options.log_file = apsi_receiver_config.log_file();
  options.silent = apsi_receiver_config.silent();
  options.query_file = apsi_receiver_config.query_file();
  options.output_file = apsi_receiver_config.output_file();
  options.params_file = apsi_receiver_config.params_file();
  options.channel = "yacl";

  options.experimental_enable_bucketize =
      apsi_receiver_config.experimental_enable_bucketize();
  options.experimental_bucket_cnt =
      apsi_receiver_config.experimental_bucket_cnt();
  options.query_batch_size = apsi_receiver_config.query_batch_size()
                                 ? apsi_receiver_config.query_batch_size()
                                 : 1;

  int* match_cnt = new int(0);

  YACL_ENFORCE_EQ(RunReceiver(options, lctx, match_cnt), 0);

  PirResultReport report;
  report.set_match_cnt(*match_cnt);

  delete match_cnt;

  return report;
}

PirResultReport RunPir(const ApsiSenderConfig& apsi_sender_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::apsi_wrapper::cli::SenderOptions options;
  options.threads = apsi_sender_config.threads();
  if (apsi_sender_config.log_level() == "all" ||
      apsi_sender_config.log_level() == "debug" ||
      apsi_sender_config.log_level() == "info" ||
      apsi_sender_config.log_level() == "warning" ||
      apsi_sender_config.log_level() == "error" ||
      apsi_sender_config.log_level() == "off") {
    options.log_level = apsi_sender_config.log_level();
  }
  options.log_file = apsi_sender_config.log_file();
  options.silent = apsi_sender_config.silent();
  options.db_file = apsi_sender_config.db_file();
  options.source_file = apsi_sender_config.source_file();
  options.params_file = apsi_sender_config.params_file();
  options.sdb_out_file = apsi_sender_config.sdb_out_file();
  options.channel = "yacl";
  options.compress = apsi_sender_config.compress();
  options.save_db_only = apsi_sender_config.save_db_only();
  if (apsi_sender_config.nonce_byte_count() != 0) {
    options.nonce_byte_count = apsi_sender_config.nonce_byte_count();
  }

  options.experimental_enable_bucketize =
      apsi_sender_config.experimental_enable_bucketize();
  options.experimental_bucket_cnt =
      apsi_sender_config.experimental_bucket_cnt();
  options.experimental_bucket_folder =
      apsi_sender_config.experimental_bucket_folder();
  if (apsi_sender_config.experimental_db_generating_process_num()) {
    options.experimental_db_generating_process_num =
        apsi_sender_config.experimental_db_generating_process_num();
  }
  if (apsi_sender_config.experimental_bucket_group_cnt()) {
    options.experimental_bucket_group_cnt =
        apsi_sender_config.experimental_bucket_group_cnt();
  }
  YACL_ENFORCE_EQ(RunSender(options, lctx), 0);

  return PirResultReport();
}

}  // namespace psi
