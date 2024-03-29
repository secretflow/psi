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

#include <omp.h>

#include <future>
#include <random>
#include <tuple>

#include "absl/strings/str_split.h"
#include "benchmark/benchmark.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

#include "psi/rr22/rr22_psi.h"

namespace {

std::tuple<std::vector<uint128_t>, std::vector<uint128_t>, std::vector<size_t>>
GenerateTestData(size_t item_size, [[maybe_unused]] double p = 0.5) {
  // uint128_t seed = yacl::MakeUint128(0, 0);
  uint128_t seed = yacl::crypto::FastRandSeed();
  yacl::crypto::Prg<uint128_t> prng(seed);

  std::vector<uint128_t> inputs_a(item_size);
  std::vector<uint128_t> inputs_b(item_size);

  prng.Fill(absl::MakeSpan(inputs_a));
  prng.Fill(absl::MakeSpan(inputs_b));

  std::mt19937 std_rand(yacl::crypto::FastRandU64());
  std::bernoulli_distribution dist(p);

  std::vector<size_t> indices;
  for (size_t i = 0; i < item_size; ++i) {
    if (dist(std_rand)) {
      // if (i % 3 == 0) {
      inputs_b[i] = inputs_a[i];
      indices.push_back(i);
    }
  }

  return std::make_tuple(inputs_a, inputs_b, indices);
}

std::shared_ptr<yacl::link::Context> CreateLinkContext(
    const std::string& party_ips, size_t self_rank) {
  std::vector<std::string> ip_list = absl::StrSplit(party_ips, ',');
  YACL_ENFORCE(ip_list.size() > 1);

  yacl::link::ContextDesc ctx_desc;
  for (size_t i = 0; i < ip_list.size(); ++i) {
    ctx_desc.parties.push_back({std::to_string(i), ip_list[i]});
  }

  return yacl::link::FactoryBrpc().CreateContext(ctx_desc, self_rank);
}

}  // namespace

static void BM_Rr22FastPsi(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    size_t n = state.range(0);

    size_t mode = state.range(1);

    std::string party_ips = "127.0.0.1:9307,127.0.0.1:9308";

    // auto lctxs = yacl::link::test::SetupWorld("ab", 2);
    std::vector<std::shared_ptr<yacl::link::Context>> lctxs(2);
    auto link0_proc = std::async([&] {
      lctxs[0] = CreateLinkContext(party_ips, 0);
      lctxs[0]->ConnectToMesh();
    });
    auto link1_proc = std::async([&] {
      lctxs[1] = CreateLinkContext(party_ips, 1);
      lctxs[1]->ConnectToMesh();
    });

    link0_proc.get();
    link1_proc.get();

    lctxs[0]->SetRecvTimeout(30 * 6 * 1000);
    lctxs[1]->SetRecvTimeout(30 * 6 * 1000);

    std::vector<uint128_t> inputs_a;
    std::vector<uint128_t> inputs_b;
    std::vector<size_t> indices;

    size_t thread_num = omp_get_num_procs();

    std::tie(inputs_a, inputs_b, indices) = GenerateTestData(n);

    state.ResumeTiming();

    psi::rr22::Rr22PsiOptions psi_options(40, thread_num, true);
    if (mode == 1) {
      psi_options.mode = psi::rr22::Rr22PsiMode::LowCommMode;
    } else if (mode == 2) {
      psi_options.mode = psi::rr22::Rr22PsiMode::FastMode;
      psi_options.malicious = true;
    }

    auto psi_sender_proc = std::async([&] {
      psi::rr22::Rr22PsiSenderInternal(psi_options, lctxs[0], inputs_a);
    });

    auto psi_receiver_proc = std::async([&] {
      return psi::rr22::Rr22PsiReceiverInternal(psi_options, lctxs[1],
                                                inputs_b);
    });

    psi_sender_proc.get();
    std::vector<size_t> indices_psi = psi_receiver_proc.get();

    SPDLOG_INFO("intersection size: {}", indices_psi.size());

    YACL_ENFORCE(indices_psi.size() == indices.size(), "{}!={}",
                 indices_psi.size(), indices.size());

    auto stats0 = lctxs[0]->GetStats();
    auto stats1 = lctxs[1]->GetStats();

    SPDLOG_INFO("stats0->sent_bytes:{},stats0->recv_bytes:{}, total:{}",
                stats0->sent_bytes.load(), stats0->recv_bytes.load(),
                (stats0->sent_bytes.load() + stats0->recv_bytes.load()));
    SPDLOG_INFO("stats1->sent_bytes:{},stats1->recv_bytes:{}, total:{}",
                stats1->sent_bytes.load(), stats1->recv_bytes.load(),
                (stats1->sent_bytes.load() + stats1->recv_bytes.load()));
  }
}

BENCHMARK(BM_Rr22FastPsi)
    ->Unit(benchmark::kMillisecond)
    ->Args({1 << 20, 0})  // fast mode
    ->Args({1 << 21, 0})
    ->Args({1 << 22, 0})
    ->Args({1 << 23, 0})
    ->Args({1 << 24, 0})
    ->Args({1 << 20, 1})  // low comm mode
    ->Args({1 << 21, 1})
    ->Args({1 << 22, 1})
    ->Args({1 << 23, 1})
    ->Args({1 << 24, 1})
    ->Args({1 << 24, 2});  // fast mode malicious
