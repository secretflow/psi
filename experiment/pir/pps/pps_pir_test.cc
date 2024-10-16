#include <set>
#include <unordered_map>

#include "client.h"
#include "gtest/gtest.h"
#include "server.h"
#include "yacl/base/dynamic_bitset.h"

namespace {

#define UNIVERSE_SIZE (1ULL << 23)
#define SET_SIZE (1ULL << 11)

#define M_UNIVERSE_SIZE (1ULL << 12)
#define M_SET_SIZE (1ULL << 6)

#define LAMBDA 1000

inline void GenerateRandomBitString(yacl::dynamic_bitset<>& bits,
                                    uint64_t len) {
  uint64_t blocks = (len + 127) / 128;
  for (std::size_t i = 0; i < blocks; ++i) {
    bits.append(yacl::crypto::RandU128());
  }
}

TEST(PIRTest, SingleBitQueryTest) {
  pir::pps::PpsPirClient pirClient(LAMBDA, UNIVERSE_SIZE, SET_SIZE);
  pir::pps::PpsPirServer pirServer(UNIVERSE_SIZE, SET_SIZE);

  pir::pps::PIRKey pirKey;
  pir::pps::PIRQueryParam pirQueryParam;
  pir::pps::PIRPuncKey pirPuncKey;
  std::set<uint64_t> deltas;
  yacl::dynamic_bitset<> bits;
  GenerateRandomBitString(bits, UNIVERSE_SIZE);
  yacl::dynamic_bitset<> h;
  uint64_t query_index = pirClient.UniformUint64();
  bool query_result;

  pirClient.Setup(pirKey, deltas);
  pirServer.Hint(pirKey, deltas, bits, h);
  pirClient.Query(query_index, pirKey, deltas, pirQueryParam, pirPuncKey);
  bool a = pirServer.Answer(pirPuncKey, bits);
  if (pirClient.Reconstruct(pirQueryParam, h, a, query_result)) {
    ASSERT_EQ(query_result, bits[query_index]);
  }
}

TEST(PIRTest, MultiBitQueryTest) {
  pir::pps::PpsPirClient pirClient(LAMBDA, M_UNIVERSE_SIZE, M_SET_SIZE);
  pir::pps::PpsPirServer pirServer(M_UNIVERSE_SIZE, M_SET_SIZE);

  std::vector<pir::pps::PIRKeyUnion> pirKey;
  yacl::dynamic_bitset<> bits;
  GenerateRandomBitString(bits, M_UNIVERSE_SIZE);
  yacl::dynamic_bitset<> h;
  pir::pps::PIRQueryParam pirParam;

  bool a_left, a_right, query_result;
  std::vector<std::unordered_set<uint64_t>> v;

  pirClient.Setup(pirKey, v);
  pirServer.Hint(pirKey, bits, h);

  for (uint i = 0; i < M_UNIVERSE_SIZE; ++i) {
    pir::pps::PIRPuncKey pirPuncKeyL, pirPuncKeyR;
    pirClient.Query(i, pirKey, v, pirParam, pirPuncKeyL, pirPuncKeyR);
    pirServer.AnswerMulti(pirPuncKeyL, pirPuncKeyR, a_left, a_right, bits);
    if (pirClient.Reconstruct(pirParam, h, a_left, a_right, query_result)) {
      ASSERT_EQ(bits[i], query_result);
    }
  }
}
}  // namespace
