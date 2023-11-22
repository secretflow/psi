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

#include "psi/psi/utils/inner_join.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/proto/psi.pb.h"

namespace psi::psi {

TEST(InnerJoinTest, Works) {
  constexpr auto input_content_alice = R"csv(id1,id2,y1
1,"b","y1_1"
1,"b","y1_2"
3,"c","y1_3"
4,"b","y1_4"
2,"a","y1_5"
2,"a","y1_6"
)csv";

  constexpr auto input_content_bob =
      R"csv(id1,id2,y2
1,"b","y2_1"
1,"b","y2_2"
1,"b","y2_3"
3,"c","y2_4"
3,"c","y2_5"
3,"c","y2_6"
2,"a","y2_7"
4,"b","y2_8"
4,"b","y2_9"
)csv";

  constexpr auto expected_output_content_alice =
      R"csv("id1","id2","y1"
1,"b","y1_1"
1,"b","y1_1"
1,"b","y1_1"
1,"b","y1_2"
1,"b","y1_2"
1,"b","y1_2"
2,"a","y1_5"
2,"a","y1_6"
3,"c","y1_3"
3,"c","y1_3"
3,"c","y1_3"
4,"b","y1_4"
4,"b","y1_4"
)csv";

  constexpr auto expected_output_content_bob =
      R"csv("id1","id2","y2"
1,"b","y2_1"
1,"b","y2_2"
1,"b","y2_3"
1,"b","y2_1"
1,"b","y2_2"
1,"b","y2_3"
2,"a","y2_7"
2,"a","y2_7"
3,"c","y2_4"
3,"c","y2_5"
3,"c","y2_6"
4,"b","y2_8"
4,"b","y2_9"
)csv";

  v2::InnerJoinConfig alice_config;

  std::filesystem::path inner_join_test_input_alice =
      std::filesystem::temp_directory_path() /
      "inner_join_test_input_alice.csv";
  std::filesystem::path inner_join_test_sorted_input_alice =
      std::filesystem::temp_directory_path() /
      "inner_join_test_sorted_input_alice.csv";
  std::filesystem::path inner_join_test_unique_input_keys_cnt_alice =
      std::filesystem::temp_directory_path() /
      "inner_join_test_unique_input_keys_cnt_alice.csv";
  std::filesystem::path inner_join_test_peer_intersection_cnt_alice =
      std::filesystem::temp_directory_path() /
      "inner_join_test_peer_intersection_cnt_alice.csv";
  std::filesystem::path inner_join_test_output_path_alice =
      std::filesystem::temp_directory_path() /
      "inner_join_test_output_path_alice.csv";

  alice_config.set_input_path(inner_join_test_input_alice.string());
  alice_config.set_sorted_input_path(
      inner_join_test_sorted_input_alice.string());
  alice_config.set_unique_input_keys_cnt_path(
      inner_join_test_unique_input_keys_cnt_alice.string());
  alice_config.set_self_intersection_cnt_path(
      inner_join_test_unique_input_keys_cnt_alice.string());
  alice_config.set_peer_intersection_cnt_path(
      inner_join_test_peer_intersection_cnt_alice.string());
  alice_config.set_output_path(inner_join_test_output_path_alice.string());

  alice_config.set_role(v2::ROLE_RECEIVER);
  *alice_config.add_keys() = "id1";
  *alice_config.add_keys() = "id2";

  v2::InnerJoinConfig bob_config;

  std::filesystem::path inner_join_test_input_bob =
      std::filesystem::temp_directory_path() / "inner_join_test_input_bob.csv";
  std::filesystem::path inner_join_test_sorted_input_bob =
      std::filesystem::temp_directory_path() /
      "inner_join_test_sorted_input_bob.csv";
  std::filesystem::path inner_join_test_unique_input_keys_cnt_bob =
      std::filesystem::temp_directory_path() /
      "inner_join_test_unique_input_keys_cnt_bob.csv";
  std::filesystem::path inner_join_test_peer_intersection_cnt_bob =
      std::filesystem::temp_directory_path() /
      "inner_join_test_peer_intersection_cnt_bob.csv";
  std::filesystem::path inner_join_test_output_path_bob =
      std::filesystem::temp_directory_path() /
      "inner_join_test_output_path_bob.csv";

  bob_config.set_input_path(inner_join_test_input_bob.string());
  bob_config.set_sorted_input_path(inner_join_test_sorted_input_bob.string());
  bob_config.set_unique_input_keys_cnt_path(
      inner_join_test_unique_input_keys_cnt_bob.string());
  bob_config.set_self_intersection_cnt_path(
      inner_join_test_unique_input_keys_cnt_bob.string());
  bob_config.set_peer_intersection_cnt_path(
      inner_join_test_peer_intersection_cnt_bob.string());
  bob_config.set_output_path(inner_join_test_output_path_bob.string());

  bob_config.set_role(v2::ROLE_SENDER);
  *bob_config.add_keys() = "id1";
  *bob_config.add_keys() = "id2";

  {
    std::ofstream file;
    file.open(alice_config.input_path());
    file << input_content_alice;
    file.close();
  }

  {
    std::ofstream file;
    file.open(bob_config.input_path());
    file << input_content_bob;
    file.close();
  }

  InnerJoinGenerateSortedInput(alice_config);
  InnerJoinGenerateUniqueInputKeysCnt(alice_config);

  InnerJoinGenerateSortedInput(bob_config);
  InnerJoinGenerateUniqueInputKeysCnt(bob_config);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      InnerJoinSyncIntersectionCnt(lctxs[idx], alice_config);
    } else {
      InnerJoinSyncIntersectionCnt(lctxs[idx], bob_config);
    }
  };

  std::vector<std::future<void>> f_links(2);

  for (size_t i = 0; i < 2; i++) {
    f_links[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    f_links[i].get();
  }

  InnerJoinGenerateIntersection(alice_config);
  InnerJoinGenerateIntersection(bob_config);

  {
    std::ifstream t(alice_config.output_path());
    std::stringstream buffer;
    buffer << t.rdbuf();
    EXPECT_EQ(buffer.str(), expected_output_content_alice);
  }
  {
    std::ifstream t(bob_config.output_path());
    std::stringstream buffer;
    buffer << t.rdbuf();
    EXPECT_EQ(buffer.str(), expected_output_content_bob);
  }

  {
    std::filesystem::remove(alice_config.input_path());
    std::filesystem::remove(alice_config.sorted_input_path());
    std::filesystem::remove(alice_config.unique_input_keys_cnt_path());
    std::filesystem::remove(alice_config.peer_intersection_cnt_path());
    std::filesystem::remove(alice_config.output_path());
  }
  {
    std::filesystem::remove(bob_config.input_path());
    std::filesystem::remove(bob_config.sorted_input_path());
    std::filesystem::remove(bob_config.unique_input_keys_cnt_path());
    std::filesystem::remove(bob_config.peer_intersection_cnt_path());
    std::filesystem::remove(bob_config.output_path());
  }
}

TEST(InnerJoinTest, OutputDifference) {
  constexpr auto sorted_input_content = R"csv(id1,id2,y1
0,"a","y1_0"
0,"b","y1_0"
0,"b","y1_0"
0,"c","y1_0"
1,"b","y1_1"
1,"b","y1_1"
1,"c","y1_0"
1,"c","y1_0"
2,"a","y1_5"
2,"a","y1_6"
3,"c","y1_3"
3,"b","y1_0"
4,"b","y1_4"
5,"a","y1_0"
5,"b","y1_0"
5,"b","y1_0"
5,"c","y1_0"
)csv";

  constexpr auto self_intersection_cnt_content =
      R"csv(id1,id2,psi_inner_join_cnt,psi_innner_join_first_index
1,"b",2,4
2,"a",2,8
3,"c",1,10
4,"b",1,12
)csv";

  constexpr auto expected_output_content = R"csv("id1","id2","y1"
0,"a","y1_0"
0,"b","y1_0"
0,"b","y1_0"
0,"c","y1_0"
1,"c","y1_0"
1,"c","y1_0"
3,"b","y1_0"
5,"a","y1_0"
5,"b","y1_0"
5,"b","y1_0"
5,"c","y1_0"
)csv";

  v2::InnerJoinConfig config;

  std::filesystem::path inner_join_test_sorted_input_file =
      std::filesystem::temp_directory_path() /
      "inner_join_test_sorted_input.csv";
  std::filesystem::path inner_join_test_self_intersection_file =
      std::filesystem::temp_directory_path() /
      "inner_join_test_self_intersection.csv";
  std::filesystem::path inner_join_test_output_path_file =
      std::filesystem::temp_directory_path() / "inner_join_test_output.csv";

  config.set_sorted_input_path(inner_join_test_sorted_input_file.string());
  config.set_self_intersection_cnt_path(
      inner_join_test_self_intersection_file.string());
  config.set_output_path(inner_join_test_output_path_file.string());

  {
    std::ofstream file;
    file.open(config.sorted_input_path());
    file << sorted_input_content;
    file.close();
  }

  {
    std::ofstream file;
    file.open(config.self_intersection_cnt_path());
    file << self_intersection_cnt_content;
    file.close();
  }

  InnerJoinGenerateDifference(config);

  {
    std::ifstream t(config.output_path());
    std::stringstream buffer;
    buffer << t.rdbuf();
    EXPECT_EQ(buffer.str(), expected_output_content);
  }

  {
    std::filesystem::remove(config.sorted_input_path());
    std::filesystem::remove(config.self_intersection_cnt_path());
    // std::filesystem::remove(config.output_path());
  }
}

}  // namespace psi::psi
