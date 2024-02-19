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

#include "psi/cryptor/hash_to_curve_elligator2.h"

#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace psi {

namespace {

[[maybe_unused]] void HexDump(yacl::ByteContainerView msg,
                              const std::string &info = "") {
  SPDLOG_INFO("{}({}): {}", info, msg.size(),
              absl::BytesToHexString(absl::string_view(
                  reinterpret_cast<const char *>(msg.data()), msg.size())));
}

// rfc9380 J.4.1. domain separation tag (DST)
constexpr char kRFC9380Curve25519RoDst[] =
    "QUUX-V01-CS02-with-curve25519_XMD:SHA-512_ELL2_RO_";

}  // namespace

TEST(Elligator2Test, HashToCurve) {
  // rfc9380 J.4.1.  curve25519_XMD:SHA-512_ELL2_RO_
  std::vector<std::string> rfc_9380_test_msgs = {"", "abc", "abcdef0123456789"};

  std::vector<std::string> rfc_9380_test_px = {
      "2de3780abb67e861289f5749d16d3e217ffa722192d16bbd9d1bfb9d112b98c0",
      "2b4419f1f2d48f5872de692b0aca72cc7b0a60915dd70bde432e826b6abc526d",
      "68ca1ea5a6acf4e9956daa101709b1eee6c1bb0df1de3b90d4602382a104c036"};

  for (size_t i = 0; i < rfc_9380_test_msgs.size(); ++i) {
    std::vector<uint8_t> px =
        HashToCurveElligator2(rfc_9380_test_msgs[i], kRFC9380Curve25519RoDst);

    EXPECT_EQ(rfc_9380_test_px[i],
              absl::BytesToHexString(absl::string_view(
                  reinterpret_cast<char *>(px.data()), px.size())));
  }
}

}  // namespace psi
