#include <set>
#include <unordered_map>

#include "benchmark/benchmark.h"
#include "client.h"
#include "server.h"
#include "yacl/base/dynamic_bitset.h"

namespace {

inline void GenerateRandomBitString(yacl::dynamic_bitset<>& bits,
                                    uint64_t len) {
  uint64_t blocks = (len + 127) / 128;
  for (std::size_t i = 0; i < blocks; ++i) {
    bits.append(yacl::crypto::RandU128());
  }
}

static void BM_PpsSingleBitPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();

    size_t n = state.range(0);
    pir::pps::PpsPirClient pirClient(n * n * 2, n);
    pir::pps::PpsPirServer pirServer(n * n * 2, n);

    pir::pps::PIRKey pirKey;
    pir::pps::PIRQueryParam pirQueryParam;
    pir::pps::PIRPuncKey pirPuncKey;
    pir::pps::PIREvalMap pirMap;
    std::set<uint64_t> deltas;
    yacl::dynamic_bitset<> bits;
    GenerateRandomBitString(bits, n * n * 2);
    yacl::dynamic_bitset<> h;
    uint64_t query_index = pirClient.UniformUint64();
    bool query_result;

    state.ResumeTiming();

    pirClient.Setup(pirKey, pirMap, deltas);
    pirServer.Hint(pirKey, deltas, bits, h);
    pirClient.Query(query_index, pirKey, pirMap, deltas, pirQueryParam,
                    pirPuncKey);
    bool a = pirServer.Answer(pirPuncKey, bits);
    pirClient.Reconstruct(pirQueryParam, h, a, query_result);
  }
}

static void BM_PpsMultiBitsPir(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();

    size_t n = state.range(0);
    pir::pps::PpsPirClient pirClient(n * n, n);
    pir::pps::PpsPirServer pirServer(n * n, n);

    std::vector<pir::pps::PIRKeyUnion> pirKey;
    yacl::dynamic_bitset<> bits;
    GenerateRandomBitString(bits, n * n);
    yacl::dynamic_bitset<> h;
    pir::pps::PIRQueryParam pirParam;

    bool a_left, a_right, query_result;
    std::vector<std::unordered_set<uint64_t>> v;

    state.ResumeTiming();

    for (uint i = 0; i < n * n; ++i) {
      pir::pps::PIRPuncKey pirPuncKeyL, pirPuncKeyR;
      pirClient.Query(i, pirKey, v, pirParam, pirPuncKeyL, pirPuncKeyR);
      pirServer.AnswerMulti(pirPuncKeyL, pirPuncKeyR, a_left, a_right, bits);
      pirClient.Reconstruct(pirParam, h, a_left, a_right, query_result);
    }
  }
}

BENCHMARK(BM_PpsSingleBitPir)
    ->Unit(benchmark::kMillisecond)
    ->Arg(1 << 8)
    ->Arg(1 << 10)
    ->Arg(1 << 12);

BENCHMARK(BM_PpsMultiBitsPir)
    ->Unit(benchmark::kMillisecond)
    ->Arg(1 << 6)
    ->Arg(1 << 8);
}  // namespace