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
// > bazel run //examples:simple_in_memory_psi -c opt -- -rank=0
// > bazel run //examples:simple_in_memory_psi -c opt -- -rank=1
// To run with non-default IP config, add -parties IP:port,IP:port to above commands
// clang-format on

#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "examples/utils.h"
#include "spdlog/spdlog.h"

#include "psi/psi/core/ecdh_psi.h"

DEFINE_uint32(data_size, 100, "size of dataset to do psi");

namespace {

std::array<std::string, 2> kDataSetPrefix = {"secret_", "flow_"};
constexpr float kDefaultSampleRate = 0.7;

// create psi dataset
//   for rank0
//     secret_[idx] with probability 30%
//     flow_[idx] with probability 70%
//   for rank1
//     flow_[idx]
//   0<=idx<data_size
std::vector<std::string> CreateSampleDataset(uint32_t data_size,
                                             uint32_t rank) {
  std::vector<std::string> ret(data_size);

  std::random_device rd;
  std::mt19937 random_gen(rd());
  // use bernoulli_distribution sample rank 1 data
  std::bernoulli_distribution bernoulli(kDefaultSampleRate);

  for (size_t idx = 0; idx < data_size; idx++) {
    if (rank == 0) {
      ret[idx] = kDataSetPrefix[static_cast<size_t>(bernoulli(random_gen))];
    } else {
      ret[idx] = kDataSetPrefix[rank];
    }

    ret[idx].append(std::to_string(idx));
  }
  return ret;
}

size_t CheckPsiSize(const std::vector<std::string>& data_set) {
  size_t flow_prefix_size = 0;
  size_t compare_length = kDataSetPrefix[1].length();
  for (const auto& item : data_set) {
    if (item.compare(0, compare_length, kDataSetPrefix[1]) == 0) {
      flow_prefix_size++;
    }
  }
  return flow_prefix_size;
}

}  // namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto lctx = MakeLink();

  std::vector<std::string> data_set =
      CreateSampleDataset(FLAGS_data_size, FLAGS_rank);

  std::vector<std::string> intersection =
      psi::psi::RunEcdhPsi(lctx, data_set, yacl::link::kAllRank);

  // output intersection size
  // psi result size is nearly 70% of the data_size
  std::cout << "intersection size:" << intersection.size() << std::endl;

  if (FLAGS_rank == 0) {
    size_t flow_prefix_size = CheckPsiSize(data_set);
    assert(flow_prefix_size == intersection.size());
    std::cout << "flow_prefix_size size:" << flow_prefix_size << std::endl;
  }

  return 0;
}
