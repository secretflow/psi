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

#include <filesystem>
#include <fstream>
#include <sstream>

#include "benchmark/benchmark.h"
#include "yacl/link/test_util.h"

#include "psi/algorithm/dkpir/entry.h"
#include "psi/utils/random_str.h"

#include "psi/proto/psi.pb.h"

namespace {
void GenerateData(const std::string& db_file, const std::string& query_file,
                  uint64_t db_size, uint64_t query_size, uint64_t label_size) {
  std::stringstream db_output, query_output;
  std::string db_header = "id,label1,label2,label3";
  std::string query_header = "id";

  // Assume that each key corresponds to two duplicate rows
  for (uint64_t i = 0; i < db_size / 2; ++i) {
    std::string id = psi::GetRandomString(64);

    if (i < query_size) {
      query_output << id;
      query_output << std::endl;
    }

    for (uint64_t j = 0; j < 2; ++j) {
      std::string label1 = psi::GetRandomString(label_size);
      std::string label2 = psi::GetRandomString(label_size);
      std::string label3 = psi::GetRandomString(label_size);

      db_output << id << "," << label1 << "," << label2 << "," << label3;
      db_output << std::endl;
    }
  }

  std::ofstream db_ofstream(db_file);
  std::ofstream query_ofstream(query_file);

  db_ofstream << db_header << std::endl;
  db_ofstream << db_output.str();

  query_ofstream << query_header << std::endl;
  query_ofstream << query_output.str();

  db_ofstream.close();
  query_ofstream.close();
}

static void BM_DkPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();

    psi::CurveType curve_type = psi::CurveType::CURVE_FOURQ;
    std::string params_file = "examples/pir/apsi/parameters/1M-11041.json";

    auto uuid_str = psi::GetRandomString();
    std::filesystem::path tmp_folder{std::filesystem::temp_directory_path() /
                                     uuid_str};
    std::filesystem::create_directories(tmp_folder);

    std::string sender_source_file = tmp_folder / "sender_source.csv";
    std::string receiver_query_file = tmp_folder / "receiver_query.csv";
    std::string value_sdb_file = tmp_folder / "value_sdb_out.db";
    std::string count_sdb_file = tmp_folder / "count_sdb_out.db";
    std::string secret_key_file = tmp_folder / "secret_key.key";
    std::string receiver_output_file = tmp_folder / "result.csv";
    std::string sender_output_file = tmp_folder / "row_count.csv";

    std::string key = "id";
    std::vector<std::string> labels = {"label1", "label2", "label3"};

    GenerateData(sender_source_file, receiver_query_file, state.range(0),
                 state.range(1), state.range(2));

    psi::dkpir::DkPirSenderOptions sender_options;
    sender_options.threads = 10;
    sender_options.curve_type = curve_type;
    sender_options.params_file = params_file;
    sender_options.source_file = sender_source_file;
    sender_options.value_sdb_out_file = value_sdb_file;
    sender_options.count_sdb_out_file = count_sdb_file;
    sender_options.secret_key_file = secret_key_file;
    sender_options.result_file = sender_output_file;
    sender_options.tmp_folder = tmp_folder.string();
    sender_options.streaming_result = false;
    sender_options.key = key;
    sender_options.labels = labels;

    psi::dkpir::DkPirReceiverOptions receiver_options;
    receiver_options.threads = 10;
    receiver_options.curve_type = curve_type;
    receiver_options.params_file = params_file;
    receiver_options.query_file = receiver_query_file;
    receiver_options.result_file = receiver_output_file;
    receiver_options.tmp_folder = tmp_folder.string();
    receiver_options.streaming_result = false;
    receiver_options.key = key;
    receiver_options.labels = labels;

    auto contexts = yacl::link::test::SetupWorld(2);

    state.ResumeTiming();

    SenderOffline(sender_options);

    std::future<int> sender =
        std::async(std::launch::async, psi::dkpir::SenderOnline,
                   std::ref(sender_options), contexts[0]);

    std::future<int> receiver =
        std::async(std::launch::async, psi::dkpir::ReceiverOnline,
                   std::ref(receiver_options), contexts[1]);

    sender.get();
    receiver.get();

    std::error_code ec;
    std::filesystem::remove_all(tmp_folder, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove temp file folder: {}, msg: {}",
                  tmp_folder.string(), ec.message());
    }
  }
}
}  // namespace

BENCHMARK(BM_DkPir)
    ->Unit(benchmark::kMillisecond)
    ->Args({1 << 19, 1000, 30})
    ->Args({1 << 19, 5000, 30})
    ->Args({1 << 19, 10000, 30});
