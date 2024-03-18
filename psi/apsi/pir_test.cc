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

#include "psi/apsi/pir.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/crypto/utils/rand.h"
#include "yacl/link/test_util.h"
#include "yacl/utils/scope_guard.h"

#include "psi/apsi/psi_params.h"
#include "psi/utils/arrow_csv_batch_provider.h"
#include "psi/utils/io.h"

namespace {

constexpr size_t kPsiStartPos = 100;

struct TestParams {
  size_t nr;
  size_t ns;
  size_t label_bytes = 16;
  bool use_filedb = true;
  bool compressed = false;
  size_t bucket_size = 1000000;
};

void WriteCsvFile(const std::string &file_name, const std::string &id_name,

                  const std::vector<std::string> &items) {
  auto out = psi::io::BuildOutputStream(psi::io::FileIoOptions(file_name));
  out->Write(fmt::format("{}\n", id_name));
  for (size_t i = 0; i < items.size(); ++i) {
    out->Write(fmt::format("{}\n", items[i]));
  }
  out->Close();
}

void WriteCsvFile(const std::string &file_name, const std::string &id_name,
                  const std::string &label_name,
                  const std::vector<std::string> &items,
                  const std::vector<std::string> &labels) {
  auto out = psi::io::BuildOutputStream(psi::io::FileIoOptions(file_name));
  out->Write(fmt::format("{},{}\n", id_name, label_name));
  for (size_t i = 0; i < items.size(); ++i) {
    out->Write(fmt::format("{},{}\n", items[i], labels[i]));
  }
  out->Close();
}

void WriteSecretKey(const std::string &ecdh_secret_key_path) {
  std::ofstream wf(ecdh_secret_key_path, std::ios::out | std::ios::binary);

  std::vector<uint8_t> oprf_key = yacl::crypto::RandBytes(32);

  wf.write((const char *)oprf_key.data(), oprf_key.size());
  wf.close();
}

std::vector<std::string> GenerateData(size_t seed, size_t item_count) {
  yacl::crypto::Prg<uint128_t> prg(seed);

  std::vector<std::string> items;

  for (size_t i = 0; i < item_count; ++i) {
    std::string item(16, '\0');
    prg.Fill(absl::MakeSpan(item.data(), item.length()));
    items.emplace_back(absl::BytesToHexString(item));
  }

  return items;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
GenerateSenderData(size_t seed, size_t item_count, size_t label_byte_count,
                   const absl::Span<std::string> &receiver_items,
                   std::vector<size_t> *intersection_idx,
                   std::vector<std::string> *intersection_label) {
  std::vector<std::string> sender_items;
  std::vector<std::string> sender_labels;

  yacl::crypto::Prg<uint128_t> prg(seed);

  for (size_t i = 0; i < item_count; ++i) {
    std::string item(16, '\0');
    std::string label((label_byte_count - 1) / 2, '\0');

    prg.Fill(absl::MakeSpan(item.data(), item.length()));
    prg.Fill(absl::MakeSpan(label.data(), label.length()));
    sender_items.emplace_back(absl::BytesToHexString(item));
    sender_labels.emplace_back(absl::BytesToHexString(label));
  }

  for (size_t i = 0; i < receiver_items.size(); i += 3) {
    if ((kPsiStartPos + i * 5) >= sender_items.size()) {
      break;
    }
    sender_items[kPsiStartPos + i * 5] = receiver_items[i];
    (*intersection_idx).emplace_back(i);

    (*intersection_label).emplace_back(sender_labels[kPsiStartPos + i * 5]);
  }

  return std::make_pair(sender_items, sender_labels);
}

}  // namespace

namespace psi::apsi {

class PirTest : public testing::TestWithParam<TestParams> {};

TEST_P(PirTest, Works) {
  auto params = GetParam();
  auto ctxs = yacl::link::test::SetupWorld(2);
  ::apsi::PSIParams psi_params = GetPsiParams(params.nr, params.ns);

  std::string tmp_store_path =
      fmt::format("data_{}_{}", yacl::crypto::FastRandU64(), params.ns);

  std::filesystem::create_directory(tmp_store_path);

  // register remove of temp dir.
  ON_SCOPE_EXIT([&] {
    if (!tmp_store_path.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(tmp_store_path, ec);
      if (ec.value() != 0) {
        SPDLOG_WARN("can not remove tmp dir: {}, msg: {}", tmp_store_path,
                    ec.message());
      }
    }
  });

  std::string client_csv_path = fmt::format("{}/client.csv", tmp_store_path);
  std::string server_csv_path = fmt::format("{}/server.csv", tmp_store_path);
  std::string id_cloumn_name = "id";
  std::string label_cloumn_name = "label";

  std::vector<size_t> intersection_idx;
  std::vector<std::string> intersection_label;

  // generate test csv data
  {
    std::vector<std::string> receiver_items =
        GenerateData(yacl::crypto::FastRandU64(), params.nr);

    std::vector<std::string> sender_keys;
    std::vector<std::string> sender_labels;

    std::tie(sender_keys, sender_labels) = GenerateSenderData(
        yacl::crypto::FastRandU64(), params.ns, params.label_bytes - 4,
        absl::MakeSpan(receiver_items), &intersection_idx, &intersection_label);

    WriteCsvFile(client_csv_path, id_cloumn_name, receiver_items);
    WriteCsvFile(server_csv_path, id_cloumn_name, label_cloumn_name,
                 sender_keys, sender_labels);
  }

  // generate 32 bytes oprf_key
  std::string oprf_key_path = fmt::format("{}/oprf_key.bin", tmp_store_path);
  WriteSecretKey(oprf_key_path);

  std::string setup_path = fmt::format("{}/setup_path", tmp_store_path);
  std::string pir_result_path =
      fmt::format("{}/pir_result.csv", tmp_store_path);

  std::vector<std::string> ids = {id_cloumn_name};
  std::vector<std::string> labels = {label_cloumn_name};

  if (params.use_filedb) {
    ::psi::PirServerConfig config;

    config.set_input_path(server_csv_path);
    config.mutable_key_columns()->Add(ids.begin(), ids.end());
    config.mutable_label_columns()->Add(labels.begin(), labels.end());

    config.mutable_apsi_server_config()->set_num_per_query(params.nr);
    config.set_label_max_len(params.label_bytes);
    config.mutable_apsi_server_config()->set_oprf_key_path(oprf_key_path);
    config.set_setup_path(setup_path);
    config.mutable_apsi_server_config()->set_compressed(params.compressed);
    config.set_bucket_size(params.bucket_size);

    ::psi::PirResultReport setup_report = PirServerSetup(config);

    EXPECT_EQ(setup_report.data_count(), params.ns);

    std::future<::psi::PirResultReport> f_server = std::async([&] {
      ::psi::PirServerConfig config;
      config.set_setup_path(setup_path);

      ::psi::PirResultReport report = PirServerOnline(ctxs[0], config);
      return report;
    });

    std::future<::psi::PirResultReport> f_client = std::async([&] {
      ::psi::PirClientConfig config;

      config.set_input_path(client_csv_path);
      config.mutable_key_columns()->Add(ids.begin(), ids.end());
      config.set_output_path(pir_result_path);

      ::psi::PirResultReport report = PirClient(ctxs[1], config);
      return report;
    });

    ::psi::PirResultReport server_report = f_server.get();
    ::psi::PirResultReport client_report = f_client.get();

    EXPECT_EQ(server_report.data_count(), params.nr);
    EXPECT_EQ(client_report.data_count(), params.nr);

  } else {
    std::future<::psi::PirResultReport> f_server = std::async([&] {
      ::psi::PirServerConfig config;

      config.set_input_path(server_csv_path);
      config.mutable_key_columns()->Add(ids.begin(), ids.end());
      config.mutable_label_columns()->Add(labels.begin(), labels.end());

      config.mutable_apsi_server_config()->set_num_per_query(params.nr);
      config.set_label_max_len(params.label_bytes);
      config.mutable_apsi_server_config()->set_compressed(params.compressed);
      config.set_bucket_size(params.bucket_size);

      ::psi::PirResultReport report = PirServerFull(ctxs[0], config);
      return report;
    });

    std::future<::psi::PirResultReport> f_client = std::async([&] {
      ::psi::PirClientConfig config;

      config.set_input_path(client_csv_path);
      config.mutable_key_columns()->Add(ids.begin(), ids.end());
      config.set_output_path(pir_result_path);

      ::psi::PirResultReport report = PirClient(ctxs[1], config);
      return report;
    });

    ::psi::PirResultReport server_report = f_server.get();
    ::psi::PirResultReport client_report = f_client.get();

    EXPECT_EQ(server_report.data_count(), params.nr);
    EXPECT_EQ(client_report.data_count(), params.nr);
  }

  std::shared_ptr<::psi::ILabeledBatchProvider> pir_result_provider =
      std::make_shared<::psi::ArrowCsvBatchProvider>(
          pir_result_path, ids, intersection_idx.size(), labels);

  // read pir_result from csv
  std::vector<std::string> pir_result_ids;
  std::vector<std::string> pir_result_labels;
  std::tie(pir_result_ids, pir_result_labels) =
      pir_result_provider->ReadNextLabeledBatch();

  // check pir result correct
  EXPECT_EQ(pir_result_ids.size(), intersection_idx.size());

  EXPECT_EQ(pir_result_labels, intersection_label);
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, PirTest,
                         testing::Values(                         //
                             TestParams{10, 10000, 32},           // 10-10K-32
                             TestParams{2048, 10000, 32},         // 2048-10K-32
                             TestParams{100, 100000, 32, false})  // 100-100K-32

);

}  // namespace psi::apsi
