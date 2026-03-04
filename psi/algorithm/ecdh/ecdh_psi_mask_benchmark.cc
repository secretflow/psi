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

#include <iostream>
#include <optional>

#include "benchmark/benchmark.h"

#include "psi/cryptor/cryptor_selector.h"

namespace {

std::vector<std::string> CreateRangeItems(size_t begin, size_t size) {
  std::vector<std::string> ret(size);
  for (size_t i = 0; i < size; i++) {
    ret[i] = std::to_string(begin + i);
  }
  return ret;
}

std::optional<psi::CurveType> GetOverrideCurveType() {
  if (const auto* env = std::getenv("OVERRIDE_CURVE")) {
    if (std::strcmp(env, "25519") == 0) {
      return psi::CurveType::CURVE_25519;
    }
    if (std::strcmp(env, "FOURQ") == 0) {
      return psi::CurveType::CURVE_FOURQ;
    }
    if (std::strcmp(env, "ELLIGATOR2") == 0) {
      return psi::CurveType::CURVE_25519_ELLIGATOR2;
    }
  }
  return {};
}

}  // namespace

static void BM_EcdhPsi(benchmark::State& state) {
  auto ecc_cryptor = CreateEccCryptor(
      GetOverrideCurveType().value_or(psi::CurveType::CURVE_FOURQ));
  for (auto _ : state) {
    state.PauseTiming();
    size_t n = state.range(0);
    auto alice_items = CreateRangeItems(1, n);
    auto bob_items = CreateRangeItems(2, n);

    state.ResumeTiming();

    auto hashed_points = ecc_cryptor->HashInputs(alice_items);
    auto masked_points = ecc_cryptor->EccMask(hashed_points);
    auto masked_items = ecc_cryptor->SerializeEcPoints(masked_points);
    std::cout << "Masked " << masked_items.size() << " items. total "
              << masked_items.size() * masked_items[0].size() << " bytes"
              << std::endl;
    auto peer_points = ecc_cryptor->DeserializeEcPoints(masked_items);
  }
}

// [256k, 512k, 1m, 2m, 4m, 8m]
BENCHMARK(BM_EcdhPsi)
    ->Arg(256 << 10)
    ->Arg(512 << 10)
    ->Arg(1 << 20)
    ->Arg(2 << 20)
    ->Arg(4 << 20)
    ->Arg(8 << 20);
