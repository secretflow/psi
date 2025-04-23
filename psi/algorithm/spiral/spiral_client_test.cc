// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/algorithm/spiral/spiral_client.h"

#include <optional>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/serialize.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {
class SpiralClientDerive : public SpiralClient {
 public:
  using SpiralClient::DecodeResponseInternal;
  using SpiralClient::DecryptMatrixGsw;
  using SpiralClient::DecryptMatrixRegev;
  using SpiralClient::EncryptMatrixGsw;
  using SpiralClient::EncryptMatrixRegev;
  using SpiralClient::GenQueryInternal;
  using SpiralClient::GetFreshGswPublicKey;
  using SpiralClient::GetFreshRegevPublicKey;
  using SpiralClient::GetRegevSample;

  explicit SpiralClientDerive(Params params, DatabaseMetaInfo database_info)
      : SpiralClient(std::move(params), std::move(database_info)) {}

  explicit SpiralClientDerive(Params params)
      : SpiralClient(std::move(params)) {}
};
constexpr size_t kMaxLoop{1};

}  // namespace

TEST(Client, QuerySerialize) {
  // get a SpiralParams
  auto params = util::GetDefaultParam();
  auto params2 = util::GetDefaultParam();

  // new client
  SpiralClientDerive client(std::move(params2), DatabaseMetaInfo{100, 256});
  client.GenPublicKeys();

  // gen random target_idx
  std::random_device rd;
  std::mt19937_64 rng(rd());

  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto query = client.GenQueryInternal(rng() % params.NumItems());

    SPDLOG_DEBUG("query seed: {}", query.seed_);

    auto buffer = query.Serialize();

    SPDLOG_INFO("query buffer size: {} bytes, {} kb", buffer.size(),
                buffer.size() / 1024);

    auto query2 = SpiralQuery::Deserialize(params, buffer);

    ASSERT_EQ(query, query2);
  }
}

TEST(Client, PolyMatrixRawRngSerialize) {
  // get a SpiralParams
  auto params = util::GetDefaultParam();
  auto params2 = util::GetDefaultParam();

  // new client
  SpiralClientDerive client(std::move(params2), DatabaseMetaInfo{100, 256});
  client.GenPublicKeys();

  // gen random target_idx
  std::random_device rd;
  std::mt19937_64 rng(rd());

  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto query = client.GenQueryInternal(rng() % params.NumItems());

    // serialize ct
    auto& ct = query.ct_;
    yacl::Buffer buffer = SerializePolyMatrixRawRng(ct, params);

    SPDLOG_DEBUG("use seed compress, buffer size {} kb", buffer.size() / 1024);

    auto seed = query.seed_;
    yacl::crypto::Prg<uint64_t> rng(seed);

    auto ct2 = DeserializePolyMatrixRawRng(buffer, params, rng);

    ASSERT_EQ(ct, ct2);
  }
}

TEST(Client, QueryRngSerialize) {
  // get a SpiralParams
  auto params = util::GetDefaultParam();
  auto params2 = util::GetDefaultParam();

  // new client
  SpiralClientDerive client(std::move(params2), DatabaseMetaInfo{100, 256});
  client.GenPublicKeys();

  // gen random target_idx
  std::random_device rd;
  std::mt19937_64 rng(rd());

  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto query = client.GenQueryInternal(rng() % params.NumItems());

    SPDLOG_DEBUG("query seed: {}", query.seed_);

    auto buffer = query.SerializeRng();

    SPDLOG_INFO("use seed compress, query buffer size: {} bytes, {} kb",
                buffer.size(), buffer.size() / 1024);

    auto query2 = SpiralQuery::DeserializeRng(params, buffer);

    ASSERT_EQ(query, query2);
  }
}

TEST(Client, PublicParamsSerialize) {
  // get a SpiralParams
  auto params = util::GetDefaultParam();
  auto params2 = util::GetDefaultParam();
  // new client
  SpiralClientDerive client(std::move(params2), DatabaseMetaInfo{100, 256});
  for (size_t i = 0; i < kMaxLoop; ++i) {
    auto pp = client.GenPublicKeys();
    auto buffer = SerializePublicKeys(params, pp);

    SPDLOG_DEBUG("public params buffer size: {} kb", buffer.size() / 1024);
    auto pp2 = DeserializePublicKeys(params, buffer);

    ASSERT_EQ(pp, pp2);
  }
}

}  // namespace psi::spiral