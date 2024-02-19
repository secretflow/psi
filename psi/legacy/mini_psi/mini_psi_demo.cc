// Copyright 2022 Ant Group Co., Ltd.
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

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "gflags/gflags.h"
#include "spdlog/spdlog.h"

#include "psi/ecdh/ecdh_psi.h"
#include "psi/legacy/mini_psi/mini_psi.h"

DEFINE_int32(role, 1, "sender:0, receiver: 1");
DEFINE_int32(rank, 0, "self rank 0/1");
DEFINE_string(in, "in.csv", "input file");
DEFINE_string(out, "out.csv", "psi out file");
DEFINE_string(id, "id", "id of the csv");
DEFINE_string(local, "127.0.0.1:1234", "local address and port");
DEFINE_string(remote, "127.0.0.1:1235", "remote address and port");
DEFINE_string(protocol, "semi-honest", "semi-honest/malicious");

namespace {
constexpr uint32_t kLinkRecvTimeout = 30 * 60 * 1000;

class CSVRow {
 public:
  std::string_view operator[](std::size_t index) const {
    return std::string_view(&m_line_[m_data_[index] + 1],
                            m_data_[index + 1] - (m_data_[index] + 1));
  }
  std::size_t size() const { return m_data_.size() - 1; }
  void readNextRow(std::istream& str) {
    std::getline(str, m_line_);

    m_data_.clear();
    m_data_.emplace_back(-1);
    std::string::size_type pos = 0;
    while ((pos = m_line_.find(',', pos)) != std::string::npos) {
      m_data_.emplace_back(pos);
      ++pos;
    }
    // This checks for a trailing comma with no data after it.
    pos = m_line_.size();
    m_data_.emplace_back(pos);
  }

 private:
  std::string m_line_;
  std::vector<int> m_data_;
};

std::istream& operator>>(std::istream& str, CSVRow& data) {
  data.readNextRow(str);
  return str;
}

std::vector<std::string> ReadCsvData(const std::string& file_name) {
  std::vector<std::string> items;
  std::ifstream file(file_name);

  CSVRow row;
  // read header
  file >> row;
  while (file >> row) {
    items.emplace_back(row[0]);
  }
  return items;
}

void WriteCsvData(const std::string& file_name,
                  const std::vector<std::string>& items) {
  std::ofstream out_file;
  out_file.open(file_name, std::ios::out);
  out_file << "id" << '\r' << std::endl;
  for (const auto& item : items) {
    out_file << item << '\r' << std::endl;
  }
  out_file.close();
}

std::shared_ptr<yacl::link::Context> CreateContext(
    int self_rank, yacl::link::ContextDesc& lctx_desc) {
  std::shared_ptr<yacl::link::Context> link_ctx;

  yacl::link::FactoryBrpc factory;
  link_ctx = factory.CreateContext(lctx_desc, self_rank);
  link_ctx->ConnectToMesh();

  return link_ctx;
}

std::shared_ptr<yacl::link::Context> CreateLinks(const std::string& local_addr,
                                                 const std::string& remote_addr,
                                                 int self_rank) {
  yacl::link::ContextDesc lctx_desc;

  // int self_rank = 0;

  if (self_rank == 0) {
    std::string id = fmt::format("party{}", 0);
    lctx_desc.parties.push_back({id, local_addr});
    id = fmt::format("party{}", 1);
    lctx_desc.parties.push_back({id, remote_addr});
  } else {
    std::string id = fmt::format("party{}", 0);
    lctx_desc.parties.push_back({id, remote_addr});
    id = fmt::format("party{}", 1);
    lctx_desc.parties.push_back({id, local_addr});
  }

  return CreateContext(self_rank, lctx_desc);
}

}  // namespace

// psi demo
//  -- sender
// ./bazel-bin/psi/psi/ecdh/mini_psi_demo --in ./100m/psi_1.csv --local
// "127.0.0.1:1234" --remote "127.0.0.1:2222" --rank 0 --role 0 --protocol
// semi-honest
//
//  -- receiver
// ./bazel-bin/psi/psi/ecdh/mini_psi_demo --in ./100m/psi_1.csv --local
// "127.0.0.1:1234" --remote "127.0.0.1:2222" --rank 1 --role 1 --protocol
// semi-honest
//
int main(int argc, char** argv) {
  gflags::AllowCommandLineReparsing();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::cout << FLAGS_role << "," << FLAGS_rank << std::endl;
  std::cout << FLAGS_in << "," << FLAGS_out << std::endl;
  std::cout << FLAGS_local << "," << FLAGS_remote << std::endl;
  std::cout << FLAGS_protocol << "," << FLAGS_id << std::endl;

  std::vector<std::string> items = ReadCsvData(FLAGS_in);
  std::cout << items.size() << std::endl;

  try {
    std::shared_ptr<yacl::link::Context> link_ctx =
        CreateLinks(FLAGS_in, FLAGS_remote, FLAGS_rank);
    link_ctx->SetRecvTimeout(kLinkRecvTimeout);

    std::string file_name = FLAGS_protocol;
    file_name.append("_").append(FLAGS_out);

    std::vector<std::string> intersection;
    if (FLAGS_protocol == "semi-honest") {
      intersection = psi::ecdh::RunEcdhPsi(link_ctx, items, 1);
      if (FLAGS_rank == 1) {
        SPDLOG_INFO("intersection size:{}", intersection.size());

        WriteCsvData(file_name, intersection);
      }
    } else if (FLAGS_protocol == "malicious") {
      if (FLAGS_role == 0) {
        psi::mini_psi::MiniPsiSendBatch(link_ctx, items);
      } else if (FLAGS_role == 1) {
        intersection = psi::mini_psi::MiniPsiRecvBatch(link_ctx, items);
        SPDLOG_INFO("intersection size:{}", intersection.size());
        WriteCsvData(file_name, intersection);
      }
    }
  } catch (std::exception& e) {
    SPDLOG_INFO("exception {}", e.what());
  }

  return 0;
}
