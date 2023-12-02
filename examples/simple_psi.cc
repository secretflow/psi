// Copyright 2021 Ant Group Co., Ltd.
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

// clang-format off
// To run the example, start two terminals:
// > bazel run //examples:simple_psi -c opt -- -rank 0 -protocol 1 -in_path examples/data/psi_1.csv -field_names id -out_path /tmp/p1.out
// > bazel run //examples:simple_psi -c opt -- -rank 1 -protocol 1 -in_path examples/data/psi_2.csv -field_names id -out_path /tmp/p2.out
// To run with non-default IP config, add -parties IP:port,IP:port to above commands
// clang-format on

#include "absl/strings/str_split.h"
#include "examples/utils.h"
#include "spdlog/spdlog.h"

#include "psi/psi/bucket_psi.h"

DEFINE_int32(protocol, 1, "select psi protocol, see `psi/proto/psi.proto`");

DEFINE_string(in_path, "data.csv", "psi data in file path ");

DEFINE_string(field_names, "id", "field names ");

DEFINE_string(out_path, "", "psi out file path");

DEFINE_bool(should_sort, false, "whether sort psi result");

DEFINE_bool(precheck_input, false, "whether precheck input dataset");

DEFINE_int32(bucket_size, 1 << 20, "hash bucket size");

DEFINE_double(bob_sub_sampling, 0.9, "dppsi bob_sub_sampling");

DEFINE_double(epsilon, 3, "dppsi epsilon");

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto lctx = MakeLink();

  auto field_list = absl::StrSplit(FLAGS_field_names, ',');

  psi::psi::BucketPsiConfig config;
  config.mutable_input_params()->set_path(FLAGS_in_path);
  config.mutable_input_params()->mutable_select_fields()->Add(
      field_list.begin(), field_list.end());
  config.mutable_input_params()->set_precheck(FLAGS_precheck_input);
  config.mutable_output_params()->set_path(FLAGS_out_path);
  config.mutable_output_params()->set_need_sort(FLAGS_should_sort);
  config.set_psi_type(static_cast<psi::psi::PsiType>(FLAGS_protocol));
  config.set_receiver_rank(0);

  if (psi::psi::DP_PSI_2PC == FLAGS_protocol) {
    psi::psi::DpPsiParams* dppsi_params = config.mutable_dppsi_params();
    dppsi_params->set_bob_sub_sampling(FLAGS_bob_sub_sampling);
    dppsi_params->set_epsilon(FLAGS_epsilon);
  }

  // one-way PSI, just one party get result
  config.set_broadcast_result(false);
  config.set_bucket_size(FLAGS_bucket_size);
  config.set_curve_type(psi::psi::CurveType::CURVE_25519);

  psi::psi::ProgressCallbacks progress_callbacks =
      [](const psi::psi::Progress::Data& data) {
        SPDLOG_INFO("progress callback-----percentage: {}", data.percentage);
      };

  try {
    psi::psi::BucketPsi bucket_psi(config, lctx);
    auto report = bucket_psi.Run(progress_callbacks);

    SPDLOG_INFO("rank:{} original_count:{} intersection_count:{}", lctx->Rank(),
                report.original_count(), report.intersection_count());
  } catch (const std::exception& e) {
    SPDLOG_ERROR("run psi failed: {}", e.what());
    return -1;
  }

  return 0;
}
