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
// > bazel run //examples:simple_dp_psi -c opt -- -rank 0 -in_path examples/data/psi_1.csv -field_names id
// > bazel run //examples:simple_dp_psi -c opt -- -rank 1 -in_path examples/data/psi_2.csv -field_names id -out_path /tmp/p2.out
// To run with non-default IP config, add -parties IP:port,IP:port to above commands
// clang-format on

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "examples/utils.h"
#include "spdlog/spdlog.h"

#include "psi/psi/core/dp_psi/dp_psi.h"
#include "psi/psi/utils/batch_provider.h"
#include "psi/psi/utils/serialize.h"

namespace {

// dp_psi_params reference from paper
#if 0
std::map<size_t, DpPsiOptions> dp_psi_params_map = {
    {1 << 11, DpPsiOptions(0.9)},   {1 << 12, DpPsiOptions(0.9)},
    {1 << 13, DpPsiOptions(0.9)},   {1 << 14, DpPsiOptions(0.9)},
    {1 << 15, DpPsiOptions(0.9)},   {1 << 16, DpPsiOptions(0.9)},
    {1 << 17, DpPsiOptions(0.9)},   {1 << 18, DpPsiOptions(0.9)},
    {1 << 19, DpPsiOptions(0.9)},   {1 << 20, DpPsiOptions(0.995)},
    {1 << 21, DpPsiOptions(0.995)}, {1 << 22, DpPsiOptions(0.995)},
    {1 << 23, DpPsiOptions(0.995)}, {1 << 24, DpPsiOptions(0.995)},
    {1 << 25, DpPsiOptions(0.995)}, {1 << 26, DpPsiOptions(0.995)},
    {1 << 27, DpPsiOptions(0.995)}, {1 << 28, DpPsiOptions(0.995)},
    {1 << 29, DpPsiOptions(0.995)}, {1 << 30, DpPsiOptions(0.995)}};
#endif

psi::psi::DpPsiOptions GetDpPsiOptions(size_t items_size) {
  double p1 = 0.9;
  if (items_size > (1 << 19)) {
    p1 = 0.995;
  }

  return psi::psi::DpPsiOptions(p1);
}

void WriteCsvData(const std::string& file_name,
                  const std::vector<std::string>& items,
                  const std::vector<size_t>& intersection_idxes) {
  std::ofstream out_file;
  out_file.open(file_name, std::ios::app);

  for (auto intersection_idx : intersection_idxes) {
    out_file << items[intersection_idx] << std::endl;
  }

  out_file.close();
}

constexpr uint32_t kLinkRecvTimeout = 30 * 60 * 1000;
constexpr uint32_t kLinkWindowSize = 16;

}  // namespace

DEFINE_int32(protocol, 1, "select psi protocol, see `psi/proto/psi.proto`");

DEFINE_string(in_path, "data.csv", "psi data in file path ");

DEFINE_string(field_names, "id", "field names ");

DEFINE_string(out_path, "", "psi out file path");

DEFINE_bool(should_sort, false, "whether sort psi result");

DEFINE_bool(precheck_input, false, "whether precheck input dataset");

DEFINE_int32(bucket_size, 1 << 20, "hash bucket size");

int main(int argc, char** argv) {  // NOLINT
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto lctx = MakeLink();

  auto field_list = absl::StrSplit(FLAGS_field_names, ',');
  auto batch_provider = std::make_shared<psi::psi::CsvBatchProvider>(
      FLAGS_in_path, field_list, 4096);

  std::vector<std::string> items;

  while (true) {
    auto batch_items = batch_provider->ReadNextBatch();

    if (batch_items.empty()) {
      break;
    }
    items.insert(items.end(), batch_items.begin(), batch_items.end());
  }

  std::vector<size_t> intersection_idx;

  lctx->SetThrottleWindowSize(kLinkWindowSize);

  lctx->SetRecvTimeout(kLinkRecvTimeout);

  if (FLAGS_rank == 0) {
    yacl::Buffer bob_items_size_buffer =
        lctx->Recv(lctx->NextRank(), fmt::format("peer items number"));
    size_t bob_items_size =
        psi::psi::utils::DeserializeSize(bob_items_size_buffer);

    psi::psi::DpPsiOptions options = GetDpPsiOptions(bob_items_size);

    size_t alice_sub_sample_size;
    size_t alice_up_sample_size;
    size_t intersection_size = psi::psi::RunDpEcdhPsiAlice(
        options, lctx, items, &alice_sub_sample_size, &alice_up_sample_size);

    SPDLOG_INFO("alice_sub_sample_size: {}", alice_sub_sample_size);
    SPDLOG_INFO("alice_up_sample_size: {}", alice_up_sample_size);
    SPDLOG_INFO("intersection_size: {}", intersection_size);
  } else if (FLAGS_rank == 1) {
    yacl::Buffer self_count_buffer =
        psi::psi::utils::SerializeSize(items.size());
    lctx->SendAsync(lctx->NextRank(), self_count_buffer,
                    fmt::format("send items count: {}", items.size()));

    psi::psi::DpPsiOptions options = GetDpPsiOptions(items.size());

    size_t bob_sub_sample_size;
    std::vector<size_t> intersection_idx =
        psi::psi::RunDpEcdhPsiBob(options, lctx, items, &bob_sub_sample_size);

    SPDLOG_INFO("bob_sub_sample_size: {}", bob_sub_sample_size);
    SPDLOG_INFO("intersection_idx size: {}", intersection_idx.size());

    WriteCsvData(FLAGS_out_path, items, intersection_idx);
  } else {
    YACL_THROW("wrong rank");
  }

  return 0;
}
