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
#include <chrono>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/util/json_util.h"
#include "perfetto.h"
#include "spdlog/spdlog.h"

#include "psi/algorithm/dkpir/entry.h"
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

  auto start = std::chrono::high_resolution_clock::now();

  std::unique_ptr<AbstractPsiParty> psi_party =
      createPsiParty(psi_config, lctx);
  PsiResultReport report = psi_party->Run();

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
  int minutes = duration.count() / 60;
  int seconds = duration.count() % 60;
  SPDLOG_INFO("PSI 总执行时间: {} 分 {} 秒", minutes, seconds);

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
                             int64_t callbacks_interval_ms) {
  google::protobuf::util::JsonPrintOptions json_print_options;
  json_print_options.preserve_proto_field_names = true;

  std::string config_json;
  YACL_ENFORCE(google::protobuf::util::MessageToJsonString(
                   bucket_psi_config, &config_json, json_print_options)
                   .ok());
  SPDLOG_INFO("LEGACY PSI config: {}", config_json);

  BucketPsi bucket_psi(bucket_psi_config, lctx);
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
  if (apsi_sender_config.experimental_db_generating_process_num() != 0) {
    options.experimental_db_generating_process_num =
        apsi_sender_config.experimental_db_generating_process_num();
  }
  if (apsi_sender_config.experimental_bucket_group_cnt() != 0) {
    options.experimental_bucket_group_cnt =
        apsi_sender_config.experimental_bucket_group_cnt();
  }
  YACL_ENFORCE_EQ(RunSender(options, lctx), 0);

  return PirResultReport();
}

PirResultReport RunDkPir(const DkPirReceiverConfig& dk_pir_receiver_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::dkpir::DkPirReceiverOptions options;

  options.threads = dk_pir_receiver_config.threads();
  options.curve_type = dk_pir_receiver_config.curve_type();
  options.skip_count_check = dk_pir_receiver_config.skip_count_check();
  if (dk_pir_receiver_config.log_level() == "all" ||
      dk_pir_receiver_config.log_level() == "debug" ||
      dk_pir_receiver_config.log_level() == "info" ||
      dk_pir_receiver_config.log_level() == "warning" ||
      dk_pir_receiver_config.log_level() == "error" ||
      dk_pir_receiver_config.log_level() == "off") {
    options.log_level = dk_pir_receiver_config.log_level();
  }
  options.log_file = dk_pir_receiver_config.log_file();
  options.params_file = dk_pir_receiver_config.params_file();
  options.query_file = dk_pir_receiver_config.query_file();
  options.result_file = dk_pir_receiver_config.result_file();
  options.tmp_folder = dk_pir_receiver_config.tmp_folder();
  options.key = dk_pir_receiver_config.key();
  options.labels =
      std::vector<std::string>(dk_pir_receiver_config.labels().begin(),
                               dk_pir_receiver_config.labels().end());

  YACL_ENFORCE_EQ(ReceiverOnline(options, lctx), 0);

  return PirResultReport();
}

PirResultReport RunDkPir(const DkPirSenderConfig& dk_pir_sender_config,
                         const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::dkpir::DkPirSenderOptions options;

  options.threads = dk_pir_sender_config.threads();
  options.curve_type = dk_pir_sender_config.curve_type();
  options.skip_count_check = dk_pir_sender_config.skip_count_check();
  if (dk_pir_sender_config.log_level() == "all" ||
      dk_pir_sender_config.log_level() == "debug" ||
      dk_pir_sender_config.log_level() == "info" ||
      dk_pir_sender_config.log_level() == "warning" ||
      dk_pir_sender_config.log_level() == "error" ||
      dk_pir_sender_config.log_level() == "off") {
    options.log_level = dk_pir_sender_config.log_level();
  }
  options.log_file = dk_pir_sender_config.log_file();
  options.params_file = dk_pir_sender_config.params_file();
  options.source_file = dk_pir_sender_config.source_file();
  options.value_sdb_out_file = dk_pir_sender_config.value_sdb_out_file();
  options.count_sdb_out_file = dk_pir_sender_config.count_sdb_out_file();
  options.secret_key_file = dk_pir_sender_config.secret_key_file();
  options.result_file = dk_pir_sender_config.result_file();
  options.tmp_folder = dk_pir_sender_config.tmp_folder();
  options.key = dk_pir_sender_config.key();
  options.labels =
      std::vector<std::string>(dk_pir_sender_config.labels().begin(),
                               dk_pir_sender_config.labels().end());

  switch (dk_pir_sender_config.mode()) {
    case psi::DkPirSenderConfig::MODE_OFFLINE: {
      YACL_ENFORCE_EQ(SenderOffline(options), 0);
      break;
    }
    case psi::DkPirSenderConfig::MODE_ONLINE: {
      YACL_ENFORCE_EQ(SenderOnline(options, lctx), 0);
      break;
    }
    default: {
      YACL_THROW("unsupported mode.");
    }
  }

  return PirResultReport();
}

namespace api {

const std::map<PsiProtocol, v2::Protocol> kV2ProtoclMap = {
    {PsiProtocol::PROTOCOL_ECDH, v2::Protocol::PROTOCOL_ECDH},
    {PsiProtocol::PROTOCOL_KKRT, v2::Protocol::PROTOCOL_KKRT},
    {PsiProtocol::PROTOCOL_RR22, v2::Protocol::PROTOCOL_RR22}};

const std::map<PsiProtocol, PsiType> kPsiTypeMap = {
    {PsiProtocol::PROTOCOL_ECDH_3PC, PsiType::ECDH_PSI_3PC},
    {PsiProtocol::PROTOCOL_ECDH_NPC, PsiType::ECDH_PSI_NPC},
    {PsiProtocol::PROTOCOL_KKRT_NPC, PsiType::KKRT_PSI_NPC},
    {PsiProtocol::PROTOCOL_DP, PsiType::DP_PSI_2PC},
};

const std::map<EllipticCurveType, CurveType> kCurTypeMap = {
    {EllipticCurveType::CURVE_INVALID_TYPE, CurveType::CURVE_INVALID_TYPE},
    {EllipticCurveType::CURVE_25519, CurveType::CURVE_25519},
    {EllipticCurveType::CURVE_FOURQ, CurveType::CURVE_FOURQ},
    {EllipticCurveType::CURVE_SM2, CurveType::CURVE_SM2},
    {EllipticCurveType::CURVE_SECP256K1, CurveType::CURVE_SECP256K1},
    {EllipticCurveType::CURVE_25519_ELLIGATOR2,
     CurveType::CURVE_25519_ELLIGATOR2},
};

const std::map<SourceType, v2::IoType> kIoTypeMap = {
    {SourceType::SOURCE_TYPE_UNSPECIFIED, v2::IoType::IO_TYPE_UNSPECIFIED},
    {SourceType::SOURCE_TYPE_FILE_CSV, v2::IoType::IO_TYPE_FILE_CSV}};

const std::map<ResultJoinType, v2::PsiConfig::AdvancedJoinType> kJoinTypeMap = {
    {ResultJoinType::JOIN_TYPE_UNSPECIFIED,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_UNSPECIFIED},
    {ResultJoinType::JOIN_TYPE_INNER_JOIN,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN},
    {ResultJoinType::JOIN_TYPE_LEFT_JOIN,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN},
    {ResultJoinType::JOIN_TYPE_RIGHT_JOIN,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN},
    {ResultJoinType::JOIN_TYPE_FULL_JOIN,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN},
    {ResultJoinType::JOIN_TYPE_DIFFERENCE,
     v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE},
};

v2::PsiConfig ExecConfToV2Conf(
    const PsiExecuteConfig& exec_config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  v2::PsiConfig v2_conf;
  // protocol
  auto* protocol_conf = v2_conf.mutable_protocol_config();
  protocol_conf->set_protocol(
      kV2ProtoclMap.at(exec_config.protocol_conf.protocol));
  protocol_conf->set_role(exec_config.protocol_conf.receiver_rank ==
                                  lctx->Rank()
                              ? v2::Role::ROLE_RECEIVER
                              : v2::Role::ROLE_SENDER);
  protocol_conf->set_broadcast_result(
      exec_config.protocol_conf.broadcast_result);
  protocol_conf->mutable_ecdh_config()->set_curve(
      kCurTypeMap.at(exec_config.protocol_conf.ecdh_params.curve));
  protocol_conf->mutable_ecdh_config()->set_batch_size(
      exec_config.protocol_conf.ecdh_params.batch_size);
  protocol_conf->mutable_kkrt_config()->set_bucket_size(
      exec_config.protocol_conf.bucket_size);
  protocol_conf->mutable_rr22_config()->set_bucket_size(
      exec_config.protocol_conf.bucket_size);
  protocol_conf->mutable_rr22_config()->set_low_comm_mode(
      exec_config.protocol_conf.rr22_params.low_comm_mode);

  // input
  v2_conf.mutable_input_config()->set_type(
      kIoTypeMap.at(exec_config.input_params.type));
  v2_conf.mutable_input_config()->set_path(exec_config.input_params.path);
  v2_conf.mutable_input_attr()->set_keys_unique(
      exec_config.input_params.keys_unique);

  // output
  v2_conf.mutable_output_attr()->set_csv_null_rep(
      exec_config.output_params.csv_null_rep);
  v2_conf.mutable_output_config()->set_type(
      kIoTypeMap.at(exec_config.output_params.type));
  v2_conf.mutable_output_config()->set_path(exec_config.output_params.path);

  v2_conf.mutable_keys()->Add(exec_config.input_params.selected_keys.begin(),
                              exec_config.input_params.selected_keys.end());
  v2_conf.set_disable_alignment(exec_config.output_params.disable_alignment);

  // recovery
  v2_conf.mutable_recovery_config()->set_enabled(
      exec_config.checkpoint_conf.enable);
  v2_conf.mutable_recovery_config()->set_folder(
      exec_config.checkpoint_conf.path);

  // join
  v2_conf.set_advanced_join_type(kJoinTypeMap.at(exec_config.join_conf.type));
  v2_conf.set_left_side(exec_config.join_conf.left_side_rank ==
                                exec_config.protocol_conf.receiver_rank
                            ? v2::Role::ROLE_RECEIVER
                            : v2::Role::ROLE_SENDER);
  v2_conf.set_check_hash_digest(true);
  return v2_conf;
}

BucketPsiConfig ExecConfToBucketConf(const PsiExecuteConfig& exec_config) {
  BucketPsiConfig bucket_conf;
  bucket_conf.set_psi_type(kPsiTypeMap.at(exec_config.protocol_conf.protocol));
  bucket_conf.set_receiver_rank(exec_config.protocol_conf.receiver_rank);
  bucket_conf.set_broadcast_result(exec_config.protocol_conf.broadcast_result);
  // input
  bucket_conf.mutable_input_params()->set_path(exec_config.input_params.path);
  bucket_conf.mutable_input_params()->mutable_select_fields()->Add(
      exec_config.input_params.selected_keys.begin(),
      exec_config.input_params.selected_keys.end());
  bucket_conf.mutable_input_params()->set_precheck(
      !exec_config.input_params.keys_unique);
  bucket_conf.mutable_output_params()->set_path(exec_config.output_params.path);
  bucket_conf.mutable_output_params()->set_need_sort(
      !exec_config.output_params.disable_alignment);
  bucket_conf.set_curve_type(
      kCurTypeMap.at(exec_config.protocol_conf.ecdh_params.curve));
  bucket_conf.set_bucket_size(exec_config.protocol_conf.bucket_size);
  bucket_conf.mutable_dppsi_params()->set_bob_sub_sampling(
      exec_config.protocol_conf.dp_params.bob_sub_sampling);
  bucket_conf.mutable_dppsi_params()->set_epsilon(
      exec_config.protocol_conf.dp_params.epsilon);

  return bucket_conf;
}

PsiExecuteReport PsiExecute(const PsiExecuteConfig& config,
                            const std::shared_ptr<yacl::link::Context>& lctx,
                            const ProgressParams& progress_params) {
  YACL_ENFORCE(config.protocol_conf.protocol !=
               PsiProtocol::PROTOCOL_UNSPECIFIED);

  if (config.protocol_conf.protocol == PsiProtocol::PROTOCOL_ECDH_3PC ||
      config.protocol_conf.protocol == PsiProtocol::PROTOCOL_ECDH_NPC ||
      config.protocol_conf.protocol == PsiProtocol::PROTOCOL_KKRT_NPC ||
      config.protocol_conf.protocol == PsiProtocol::PROTOCOL_DP) {
    auto bucket_conf = ExecConfToBucketConf(config);
    auto report = RunLegacyPsi(bucket_conf, lctx, progress_params.hook,
                               progress_params.interval_ms);
    return PsiExecuteReport{
        .original_count = report.original_count(),
        .intersection_count = report.intersection_count(),
        .original_unique_count = report.original_key_count(),
        .intersection_unique_count = report.intersection_key_count()};
  } else {
    auto v2_conf = ExecConfToV2Conf(config, lctx);
    auto report = ::psi::RunPsi(v2_conf, lctx);
    return PsiExecuteReport{
        .original_count = report.original_count(),
        .intersection_count = report.intersection_count(),
        .original_unique_count = report.original_key_count(),
        .intersection_unique_count = report.intersection_key_count()};
  }
}

const std::map<ub::UbPsiExecuteConfig::Mode, v2::UbPsiConfig::Mode> kModeMap = {
    {ub::UbPsiExecuteConfig::Mode::MODE_UNSPECIFIED,
     v2::UbPsiConfig::MODE_UNSPECIFIED},
    {ub::UbPsiExecuteConfig::Mode::MODE_OFFLINE_GEN_CACHE,
     v2::UbPsiConfig::MODE_OFFLINE_GEN_CACHE},
    {ub::UbPsiExecuteConfig::Mode::MODE_OFFLINE_TRANSFER_CACHE,
     v2::UbPsiConfig::MODE_OFFLINE_TRANSFER_CACHE},
    {ub::UbPsiExecuteConfig::Mode::MODE_OFFLINE, v2::UbPsiConfig::MODE_OFFLINE},
    {ub::UbPsiExecuteConfig::Mode::MODE_ONLINE, v2::UbPsiConfig::MODE_ONLINE},
    {ub::UbPsiExecuteConfig::Mode::MODE_FULL, v2::UbPsiConfig::MODE_FULL},
};

namespace internal {
v2::UbPsiConfig UbExecConfToUbconf(
    const ub::UbPsiExecuteConfig& exec_config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  YACL_ENFORCE(exec_config.role != ub::UbPsiRole::ROLE_UNSPECIFIED);
  v2::UbPsiConfig ub_conf;
  ub_conf.set_mode(kModeMap.at(exec_config.mode));
  ub_conf.set_role(exec_config.role == ub::UbPsiRole::ROLE_SERVER
                       ? v2::Role::ROLE_SERVER
                       : v2::Role::ROLE_CLIENT);
  ub_conf.set_server_secret_key_path(exec_config.server_params.secret_key_path);
  ub_conf.set_cache_path(exec_config.cache_path);
  ub_conf.set_server_get_result(exec_config.server_receive_result);
  ub_conf.set_client_get_result(exec_config.client_receive_result);

  // input
  ub_conf.mutable_input_config()->set_type(
      kIoTypeMap.at(exec_config.input_params.type));
  ub_conf.mutable_input_config()->set_path(exec_config.input_params.path);
  ub_conf.mutable_input_attr()->set_keys_unique(
      exec_config.input_params.keys_unique);

  // output
  ub_conf.mutable_output_attr()->set_csv_null_rep(
      exec_config.output_params.csv_null_rep);
  ub_conf.mutable_output_config()->set_type(
      kIoTypeMap.at(exec_config.output_params.type));
  ub_conf.mutable_output_config()->set_path(exec_config.output_params.path);

  ub_conf.mutable_keys()->Add(exec_config.input_params.selected_keys.begin(),
                              exec_config.input_params.selected_keys.end());
  ub_conf.set_disable_alignment(exec_config.output_params.disable_alignment);

  // join
  ub_conf.set_advanced_join_type(kJoinTypeMap.at(exec_config.join_conf.type));
  auto left_side = v2::Role::ROLE_CLIENT;
  if (exec_config.role == ub::UbPsiRole::ROLE_SERVER) {
    if (exec_config.join_conf.left_side_rank == lctx->Rank()) {
      left_side = v2::Role::ROLE_SERVER;
    }
  } else {
    if (exec_config.join_conf.left_side_rank != lctx->Rank()) {
      left_side = v2::Role::ROLE_SERVER;
    }
  }
  ub_conf.set_left_side(left_side);

  return ub_conf;
}
}  // namespace internal

PsiExecuteReport UbPsiExecute(
    const ub::UbPsiExecuteConfig& config,
    const std::shared_ptr<yacl::link::Context>& lctx) {
  auto ub_conf = internal::UbExecConfToUbconf(config, lctx);
  auto report = ::psi::RunUbPsi(ub_conf, lctx);
  return PsiExecuteReport{
      .original_count = report.original_count(),
      .intersection_count = report.intersection_count(),
      .original_unique_count = report.original_key_count(),
      .intersection_unique_count = report.intersection_key_count()};
}

}  // namespace api

}  // namespace psi
