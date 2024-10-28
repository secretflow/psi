#include "ggm_pset.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <unordered_set>

#include "gtest/gtest.h"

namespace {

constexpr uint32_t UNIVERSE_SIZE = (1ULL << 23);
constexpr uint32_t SET_SIZE = (1ULL << 11);
constexpr uint32_t LAMBDA = 1000;

TEST(PIRTest, GgmPsetGenAbortTest) {
  pir::pps::PPS pps(UNIVERSE_SIZE, SET_SIZE);
  uint32_t error_lambda = 0;
  ASSERT_DEATH(pps.Gen(error_lambda),
               "Running lambda iterations of the loop unsuccessfully!");
}

TEST(PIRTest, GgmPsetEvalTest) {
  pir::pps::PPS pps(UNIVERSE_SIZE, SET_SIZE);
  pir::pps::PIRKey k = pps.Gen(LAMBDA);
  pps.EvalMap(k);
  ASSERT_TRUE(pps.getMap().size() == SET_SIZE);

  std::set<uint64_t> pseudorandom_set;
  std::unordered_set<uint64_t> pseudorandom_unordered_sets;
  pps.Eval(k, pseudorandom_set);
  pps.Eval(k, pseudorandom_unordered_sets);

  ASSERT_TRUE(pseudorandom_set.size() == SET_SIZE);
  ASSERT_TRUE(pseudorandom_unordered_sets.size() == SET_SIZE);
}

TEST(PIRTest, GgmPsetPuncTest) {
  pir::pps::PPS pps(UNIVERSE_SIZE, SET_SIZE);
  pir::pps::PIRKey k = pps.Gen(LAMBDA);
  pps.EvalMap(k);
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis(0, SET_SIZE - 1);
  uint64_t rand = std::next(pps.getMap().begin(), dis(gen))->first;
  ;
  pir::pps::PIRPuncKey sk_punc;
  std::set<uint64_t> pseudorandom_set;
  pps.Punc(rand, k, sk_punc);
  pps.Eval(sk_punc, pseudorandom_set);
  pir::pps::PIREvalMap map = pps.getMap();
  for (uint64_t v : pseudorandom_set) {
    ASSERT_NE(map.find(v), map.end());
  }
  uint64_t error_value = UNIVERSE_SIZE + 1;
  ASSERT_DEATH(pps.Punc(error_value, k, sk_punc),
               "i is not in pseudorandom sets");
}
}  // namespace
