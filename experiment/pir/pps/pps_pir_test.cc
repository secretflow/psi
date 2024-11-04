// Copyright 2024 The secretflow authors.
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

#include <future>
#include <set>
#include <unordered_map>

#include "client.h"
#include "gtest/gtest.h"
#include "receiver.h"
#include "sender.h"
#include "server.h"
#include "yacl/base/buffer.h"
#include "yacl/base/dynamic_bitset.h"
#include "yacl/link/context.h"
#include "yacl/link/test_util.h"

namespace {

constexpr uint32_t UNIVERSE_SIZE = (1ULL << 23);
constexpr uint32_t SET_SIZE = (1ULL << 11);

constexpr uint32_t M_UNIVERSE_SIZE = (1ULL << 12);
constexpr uint32_t M_SET_SIZE = (1ULL << 6);

constexpr uint32_t LAMBDA = 1000;

inline void GenerateRandomBitString(yacl::dynamic_bitset<>& bits,
                                    uint64_t len) {
  uint64_t blocks = (len + 127) / 128;
  for (std::size_t i = 0; i < blocks; ++i) {
    bits.append(yacl::crypto::RandU128());
  }
}

TEST(PIRTest, SingleBitQueryTest) {
  pir::pps::PpsPirClient pirClient(LAMBDA, UNIVERSE_SIZE, SET_SIZE);
  pir::pps::PpsPirServer pirOfflineServer(UNIVERSE_SIZE, SET_SIZE);
  pir::pps::PpsPirServer pirOnlineServer(UNIVERSE_SIZE, SET_SIZE);

  pir::pps::PIRKey pirKey, pirKeyOffline;
  pir::pps::PIRQueryParam pirQueryParam;
  pir::pps::PIRPuncKey pirPuncKey, pirPuncKeyOnline;
  std::set<uint64_t> deltas, deltasOffline;
  yacl::dynamic_bitset<> bits;
  GenerateRandomBitString(bits, UNIVERSE_SIZE);
  yacl::dynamic_bitset<> h, hOffline;
  uint64_t query_index = pirClient.GetRandomU64Less();
  bool query_result;

  constexpr int kWorldSize = 2;
  const auto contextsOffline = yacl::link::test::SetupWorld(kWorldSize);
  const auto contextsOnline = yacl::link::test::SetupWorld(kWorldSize);

  pirClient.Setup(pirKey, deltas);

  std::future<void> sender_future =
      std::async(std::launch::async, pir::pps::ClientSendToOfflineServer,
                 std::ref(pirKey), std::ref(deltas), contextsOffline[0]);

  std::future<void> recver_future = std::async(
      std::launch::async, pir::pps::OfflineServerRecvFromClient,
      std::ref(pirKeyOffline), std::ref(deltasOffline), contextsOffline[1]);

  sender_future.get();
  recver_future.get();

  pirOfflineServer.Hint(pirKeyOffline, deltasOffline, bits, hOffline);

  sender_future =
      std::async(std::launch::async, pir::pps::OfflineServerSendToClient,
                 std::ref(hOffline), contextsOffline[0]);

  recver_future =
      std::async(std::launch::async, pir::pps::ClientRecvFromOfflineServer,
                 std::ref(h), contextsOffline[1]);

  sender_future.get();
  recver_future.get();

  pirClient.Query(query_index, pirKey, deltas, pirQueryParam, pirPuncKey);

  sender_future =
      std::async(std::launch::async, pir::pps::ClientSendToOnlineServer,
                 std::ref(pirPuncKey), contextsOnline[0]);

  recver_future =
      std::async(std::launch::async, pir::pps::OnlineServerRecvFromClient,
                 std::ref(pirPuncKeyOnline), contextsOnline[1]);

  sender_future.get();
  recver_future.get();

  bool a = pirOnlineServer.Answer(pirPuncKeyOnline, bits);
  bool aClient;

  sender_future =
      std::async(std::launch::async, pir::pps::OnlineServerSendToClient,
                 std::ref(a), contextsOnline[0]);

  recver_future =
      std::async(std::launch::async, pir::pps::ClientRecvFromOnlineServer,
                 std::ref(aClient), contextsOnline[1]);

  sender_future.get();
  recver_future.get();

  if (pirClient.Reconstruct(pirQueryParam, h, aClient, query_result) !=
      PIR_ABORT) {
    ASSERT_EQ(query_result, bits[query_index]);
  }
}

TEST(PIRTest, MultiBitQueryTest) {
  pir::pps::PpsPirClient pirClient(LAMBDA, M_UNIVERSE_SIZE, M_SET_SIZE);
  pir::pps::PpsPirServer pirOfflineServer(M_UNIVERSE_SIZE, M_SET_SIZE);
  pir::pps::PpsPirServer pirOnlineServer(M_UNIVERSE_SIZE, M_SET_SIZE);

  std::vector<pir::pps::PIRKeyUnion> pirKey, pirKeyOffline;
  yacl::dynamic_bitset<> bits;
  GenerateRandomBitString(bits, M_UNIVERSE_SIZE);
  yacl::dynamic_bitset<> h, hOffline;
  pir::pps::PIRQueryParam pirParam;

  bool aLeft, aRight, aLeftOnline, aRightOnline, queryResult;
  std::vector<std::unordered_set<uint64_t>> v;

  constexpr int kWorldSize = 2;
  const auto contextsOffline = yacl::link::test::SetupWorld(kWorldSize);
  const auto contextsOnline = yacl::link::test::SetupWorld(kWorldSize);

  pirClient.Setup(pirKey, v);

  std::future<void> sender_future =
      std::async(std::launch::async, pir::pps::ClientSendToOfflineServerM,
                 std::ref(pirKey), contextsOffline[0]);

  std::future<void> recver_future =
      std::async(std::launch::async, pir::pps::OfflineServerRecvFromClientM,
                 std::ref(pirKeyOffline), contextsOffline[1]);

  sender_future.get();
  recver_future.get();
  pirOfflineServer.Hint(pirKeyOffline, bits, hOffline);

  sender_future =
      std::async(std::launch::async, pir::pps::OfflineServerSendToClient,
                 std::ref(hOffline), contextsOffline[0]);

  recver_future =
      std::async(std::launch::async, pir::pps::ClientRecvFromOfflineServer,
                 std::ref(h), contextsOffline[1]);

  sender_future.get();
  recver_future.get();

  for (uint i = 0; i < M_UNIVERSE_SIZE; ++i) {
    pir::pps::PIRPuncKey pirPuncKeyL, pirPuncKeyR;
    pir::pps::PIRPuncKey pirPuncKeyLOnline, pirPuncKeyROnline;

    pirClient.Query(i, pirKey, v, pirParam, pirPuncKeyL, pirPuncKeyR);

    sender_future =
        std::async(std::launch::async, pir::pps::ClientSendToOnlineServer,
                   std::ref(pirPuncKeyL), contextsOnline[0]);

    recver_future =
        std::async(std::launch::async, pir::pps::OnlineServerRecvFromClient,
                   std::ref(pirPuncKeyLOnline), contextsOnline[1]);

    sender_future.get();
    recver_future.get();

    sender_future =
        std::async(std::launch::async, pir::pps::ClientSendToOnlineServer,
                   std::ref(pirPuncKeyR), contextsOnline[0]);

    recver_future =
        std::async(std::launch::async, pir::pps::OnlineServerRecvFromClient,
                   std::ref(pirPuncKeyROnline), contextsOnline[1]);

    sender_future.get();
    recver_future.get();

    pirOnlineServer.AnswerMulti(pirPuncKeyLOnline, pirPuncKeyROnline,
                                aLeftOnline, aRightOnline, bits);

    sender_future =
        std::async(std::launch::async, pir::pps::OnlineServerSendToClient,
                   std::ref(aLeftOnline), contextsOnline[0]);

    recver_future =
        std::async(std::launch::async, pir::pps::ClientRecvFromOnlineServer,
                   std::ref(aLeft), contextsOnline[1]);

    sender_future.get();
    recver_future.get();

    sender_future =
        std::async(std::launch::async, pir::pps::OnlineServerSendToClient,
                   std::ref(aRightOnline), contextsOnline[0]);

    recver_future =
        std::async(std::launch::async, pir::pps::ClientRecvFromOnlineServer,
                   std::ref(aRight), contextsOnline[1]);

    sender_future.get();
    recver_future.get();

    if (pirClient.Reconstruct(pirParam, h, aLeft, aRight, queryResult) !=
        PIR_ABORT) {
      ASSERT_EQ(bits[i], queryResult);
    }
  }
}
}  // namespace
