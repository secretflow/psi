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

// clang-format off
// To run the example, start two terminals:
// > bazel run //examples/pir:generate_pir_data -c opt -- -data_count 10000 -label_len 32 -server_out_path pir_server.csv -client_out_path pir_client.csv
// clang-format on

#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "fmt/format.h"
#include "gflags/gflags.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"

DEFINE_int32(data_count, 100000, "example data count");

DEFINE_int32(label_len, 288, "label data length");

DEFINE_double(query_rate, 0.001, "rate of client data in serer data num");

DEFINE_string(server_out_path, "pir_server_data.csv",
              "[out] server output path for pir example data");

DEFINE_string(client_out_path, "pir_client_data.csv",
              "[out] client output path for pir example data");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  size_t alice_item_size = FLAGS_data_count;
  size_t label_size = std::max<size_t>(1, FLAGS_label_len / 2);

  std::mt19937 rand(yacl::crypto::FastRandU64());

  double q1 = FLAGS_query_rate;

  SPDLOG_INFO("sample bernoulli_distribution: {}", q1);

  std::bernoulli_distribution dist1(q1);

  std::vector<size_t> bernoulli_items_idx;

  std::string id1_data = "111111";

  std::ofstream psi1_out_file;
  std::ofstream psi2_out_file;
  psi1_out_file.open(FLAGS_server_out_path, std::ios::out);
  psi2_out_file.open(FLAGS_client_out_path, std::ios::out);

  psi1_out_file << "id,id1,label,label1" << '\r' << std::endl;
  psi2_out_file << "id,id1" << '\r' << std::endl;

  for (size_t idx = 0; idx < alice_item_size; idx++) {
    std::string a_item = fmt::format("{:010d}{:08d}", idx, idx + 900000000);
    std::string b_item;
    if (dist1(rand)) {
      psi2_out_file << a_item << "," << id1_data << '\r' << std::endl;
    }
    std::vector<uint8_t> label_bytes = yacl::crypto::RandBytes(label_size);
    std::vector<uint8_t> label_bytes2 = yacl::crypto::RandBytes(label_size);
    psi1_out_file << a_item << "," << id1_data << ","
                  << absl::BytesToHexString(absl::string_view(
                         reinterpret_cast<char *>(label_bytes.data()),
                         label_bytes.size()))
                  << ","
                  << absl::BytesToHexString(absl::string_view(
                         reinterpret_cast<char *>(label_bytes2.data()),
                         label_bytes2.size()))
                  << '\r' << std::endl;
  }

  psi1_out_file.close();
  psi2_out_file.close();

  return 0;
}
