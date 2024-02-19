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

#include "psi/utils/advanced_join.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "yacl/link/test_util.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

void SaveFile(const std::string& path, const std::string& content) {
  std::ofstream file;
  file.open(path);
  file << content;
  file.close();
}

std::string LoadFile(const std::string& path) {
  std::ifstream t(path);
  std::stringstream buffer;
  buffer << t.rdbuf();
  return buffer.str();
}

constexpr auto kInputContentReceiver = R"csv(id1,id2,y1
3,"d","y1_7"
1,"b","y1_1"
1,"b","y1_2"
3,"c","y1_3"
3,"d","y1_8"
4,"b","y1_4"
2,"a","y1_5"
2,"a","y1_6"
5,"c","y1_9"
3,"d","y1_10"
)csv";

constexpr auto kInputContentSender = R"csv(id1,id2,y2
5,"b","y2_10"
1,"b","y2_1"
1,"b","y2_2"
1,"b","y2_3"
3,"c","y2_4"
3,"c","y2_5"
3,"c","y2_6"
6,"c","y2_12"
2,"a","y2_7"
4,"b","y2_8"
4,"b","y2_9"
5,"b","y2_11"
)csv";

constexpr auto kPSIOuputContentReceiver =
    R"csv(id1,id2,psi_advanced_join_cnt,psi_advanced_join_first_index
1,"b",2,0
2,"a",2,2
3,"c",1,4
4,"b",1,5
)csv";

constexpr auto kPSIOuputContentSender =
    R"csv(id1,id2,psi_advanced_join_cnt,psi_advanced_join_first_index
1,"b",3,0
2,"a",1,3
3,"c",3,4
4,"b",2,7
)csv";

TEST(AdvancedJoinTest, AdvancedJoinPreprocess) {
  constexpr auto expected_unique_input_keys_cnt_content =
      R"csv(id1,id2,psi_advanced_join_cnt,psi_advanced_join_first_index
1,"b",2,0
2,"a",2,2
3,"c",1,4
3,"d",3,5
4,"b",1,8
5,"c",1,9
)csv";

  std::filesystem::path input_path =
      std::filesystem::temp_directory_path() / "input.csv";

  SaveFile(input_path, kInputContentReceiver);

  AdvancedJoinConfig config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"}, input_path, "",
      std::filesystem::temp_directory_path());
  AdvancedJoinPreprocess(&config);

  EXPECT_EQ(LoadFile(config.unique_input_keys_cnt_path),
            expected_unique_input_keys_cnt_content);
  EXPECT_EQ(config.self_total_cnt, 10);
}

TEST(AdvancedJoinTest, InnerJoin) {
  std::filesystem::path receiver_input_path =
      std::filesystem::temp_directory_path() / "receiver_input.csv";
  std::filesystem::path receiver_output_path =
      std::filesystem::temp_directory_path() / "receiver_inner_join_output.csv";
  SaveFile(receiver_input_path, kInputContentReceiver);

  std::filesystem::path sender_input_path =
      std::filesystem::temp_directory_path() / "sender_input.csv";
  std::filesystem::path sender_output_path =
      std::filesystem::temp_directory_path() / "sender_inner_join_output.csv";
  SaveFile(sender_input_path, kInputContentSender);

  AdvancedJoinConfig receiver_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      receiver_input_path, receiver_output_path,
      std::filesystem::temp_directory_path());
  AdvancedJoinConfig sender_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_INNER_JOIN, v2::ROLE_SENDER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      sender_input_path, sender_output_path,
      std::filesystem::temp_directory_path());

  AdvancedJoinPreprocess(&receiver_config);
  AdvancedJoinPreprocess(&sender_config);

  SaveFile(receiver_config.self_intersection_cnt_path,
           kPSIOuputContentReceiver);
  SaveFile(sender_config.self_intersection_cnt_path, kPSIOuputContentSender);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      AdvancedJoinSync(lctxs[idx], &receiver_config);
    } else {
      AdvancedJoinSync(lctxs[idx], &sender_config);
    }
  };

  std::vector<std::future<void>> fs(2);

  for (size_t i = 0; i < 2; i++) {
    fs[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    try {
      fs[i].get();
    } catch (...) {
      std::exception_ptr ep = std::current_exception();
      std::rethrow_exception(ep);
    }
  }

  EXPECT_EQ(receiver_config.self_intersection_cnt, 6);
  EXPECT_EQ(receiver_config.self_difference_cnt, 4);
  EXPECT_EQ(receiver_config.peer_difference_cnt, 0);

  EXPECT_EQ(sender_config.self_intersection_cnt, 9);
  EXPECT_EQ(sender_config.self_difference_cnt, 3);
  EXPECT_EQ(sender_config.peer_difference_cnt, 0);

  AdvancedJoinGenerateResult(receiver_config);
  AdvancedJoinGenerateResult(sender_config);

  constexpr auto expected_output_content_receiver = R"csv("id1","id2","y1"
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

  constexpr auto expected_output_content_sender = R"csv("id1","id2","y2"
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

  EXPECT_EQ(LoadFile(receiver_config.output_path),
            expected_output_content_receiver);
  EXPECT_EQ(LoadFile(sender_config.output_path),
            expected_output_content_sender);
}

TEST(AdvancedJoinTest, LeftJoin) {
  std::filesystem::path receiver_input_path =
      std::filesystem::temp_directory_path() / "receiver_input.csv";
  std::filesystem::path receiver_output_path =
      std::filesystem::temp_directory_path() / "receiver_left_join_output.csv";
  SaveFile(receiver_input_path, kInputContentReceiver);

  std::filesystem::path sender_input_path =
      std::filesystem::temp_directory_path() / "sender_input.csv";
  std::filesystem::path sender_output_path =
      std::filesystem::temp_directory_path() / "sender_left_join_output.csv";
  SaveFile(sender_input_path, kInputContentSender);

  AdvancedJoinConfig receiver_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      receiver_input_path, receiver_output_path,
      std::filesystem::temp_directory_path());
  AdvancedJoinConfig sender_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_LEFT_JOIN, v2::ROLE_SENDER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      sender_input_path, sender_output_path,
      std::filesystem::temp_directory_path());

  AdvancedJoinPreprocess(&receiver_config);
  AdvancedJoinPreprocess(&sender_config);

  SaveFile(receiver_config.self_intersection_cnt_path,
           kPSIOuputContentReceiver);
  SaveFile(sender_config.self_intersection_cnt_path, kPSIOuputContentSender);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      AdvancedJoinSync(lctxs[idx], &receiver_config);
    } else {
      AdvancedJoinSync(lctxs[idx], &sender_config);
    }
  };

  std::vector<std::future<void>> fs(2);

  for (size_t i = 0; i < 2; i++) {
    fs[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    try {
      fs[i].get();
    } catch (...) {
      std::exception_ptr ep = std::current_exception();
      std::rethrow_exception(ep);
    }
  }

  EXPECT_EQ(receiver_config.self_intersection_cnt, 6);
  EXPECT_EQ(receiver_config.self_difference_cnt, 4);
  EXPECT_EQ(receiver_config.peer_difference_cnt, 3);

  EXPECT_EQ(sender_config.self_intersection_cnt, 9);
  EXPECT_EQ(sender_config.self_difference_cnt, 3);
  EXPECT_EQ(sender_config.peer_difference_cnt, 4);

  AdvancedJoinGenerateResult(receiver_config);
  AdvancedJoinGenerateResult(sender_config);

  constexpr auto expected_output_content_receiver = R"csv("id1","id2","y1"
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
3,"d","y1_7"
3,"d","y1_8"
3,"d","y1_10"
5,"c","y1_9"
)csv";

  constexpr auto expected_output_content_sender = R"csv("id1","id2","y2"
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
NA,NA,NA
NA,NA,NA
NA,NA,NA
NA,NA,NA
)csv";

  EXPECT_EQ(LoadFile(receiver_config.output_path),
            expected_output_content_receiver);
  EXPECT_EQ(LoadFile(sender_config.output_path),
            expected_output_content_sender);
}

TEST(AdvancedJoinTest, RightJoin) {
  std::filesystem::path receiver_input_path =
      std::filesystem::temp_directory_path() / "receiver_input.csv";
  std::filesystem::path receiver_output_path =
      std::filesystem::temp_directory_path() / "receiver_right_join_output.csv";
  SaveFile(receiver_input_path, kInputContentReceiver);

  std::filesystem::path sender_input_path =
      std::filesystem::temp_directory_path() / "sender_input.csv";
  std::filesystem::path sender_output_path =
      std::filesystem::temp_directory_path() / "sender_right_join_output.csv";
  SaveFile(sender_input_path, kInputContentSender);

  AdvancedJoinConfig receiver_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      receiver_input_path, receiver_output_path,
      std::filesystem::temp_directory_path());
  AdvancedJoinConfig sender_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_RIGHT_JOIN, v2::ROLE_SENDER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      sender_input_path, sender_output_path,
      std::filesystem::temp_directory_path());

  AdvancedJoinPreprocess(&receiver_config);
  AdvancedJoinPreprocess(&sender_config);

  SaveFile(receiver_config.self_intersection_cnt_path,
           kPSIOuputContentReceiver);
  SaveFile(sender_config.self_intersection_cnt_path, kPSIOuputContentSender);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      AdvancedJoinSync(lctxs[idx], &receiver_config);
    } else {
      AdvancedJoinSync(lctxs[idx], &sender_config);
    }
  };

  std::vector<std::future<void>> fs(2);

  for (size_t i = 0; i < 2; i++) {
    fs[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    try {
      fs[i].get();
    } catch (...) {
      std::exception_ptr ep = std::current_exception();
      std::rethrow_exception(ep);
    }
  }

  EXPECT_EQ(receiver_config.self_intersection_cnt, 6);
  EXPECT_EQ(receiver_config.self_difference_cnt, 4);
  EXPECT_EQ(receiver_config.peer_difference_cnt, 3);

  EXPECT_EQ(sender_config.self_intersection_cnt, 9);
  EXPECT_EQ(sender_config.self_difference_cnt, 3);
  EXPECT_EQ(sender_config.peer_difference_cnt, 4);

  AdvancedJoinGenerateResult(receiver_config);
  AdvancedJoinGenerateResult(sender_config);

  constexpr auto expected_output_content_receiver = R"csv("id1","id2","y1"
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
NA,NA,NA
NA,NA,NA
NA,NA,NA
)csv";

  constexpr auto expected_output_content_sender = R"csv("id1","id2","y2"
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
5,"b","y2_10"
5,"b","y2_11"
6,"c","y2_12"
)csv";

  EXPECT_EQ(LoadFile(receiver_config.output_path),
            expected_output_content_receiver);
  EXPECT_EQ(LoadFile(sender_config.output_path),
            expected_output_content_sender);
}

TEST(AdvancedJoinTest, FullJoin) {
  std::filesystem::path receiver_input_path =
      std::filesystem::temp_directory_path() / "receiver_input.csv";
  std::filesystem::path receiver_output_path =
      std::filesystem::temp_directory_path() / "receiver_full_join_output.csv";
  SaveFile(receiver_input_path, kInputContentReceiver);

  std::filesystem::path sender_input_path =
      std::filesystem::temp_directory_path() / "sender_input.csv";
  std::filesystem::path sender_output_path =
      std::filesystem::temp_directory_path() / "sender_full_join_output.csv";
  SaveFile(sender_input_path, kInputContentSender);

  AdvancedJoinConfig receiver_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      receiver_input_path, receiver_output_path,
      std::filesystem::temp_directory_path());
  AdvancedJoinConfig sender_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_FULL_JOIN, v2::ROLE_SENDER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      sender_input_path, sender_output_path,
      std::filesystem::temp_directory_path());

  AdvancedJoinPreprocess(&receiver_config);
  AdvancedJoinPreprocess(&sender_config);

  SaveFile(receiver_config.self_intersection_cnt_path,
           kPSIOuputContentReceiver);
  SaveFile(sender_config.self_intersection_cnt_path, kPSIOuputContentSender);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      AdvancedJoinSync(lctxs[idx], &receiver_config);
    } else {
      AdvancedJoinSync(lctxs[idx], &sender_config);
    }
  };

  std::vector<std::future<void>> fs(2);

  for (size_t i = 0; i < 2; i++) {
    fs[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    try {
      fs[i].get();
    } catch (...) {
      std::exception_ptr ep = std::current_exception();
      std::rethrow_exception(ep);
    }
  }

  EXPECT_EQ(receiver_config.self_intersection_cnt, 6);
  EXPECT_EQ(receiver_config.self_difference_cnt, 4);
  EXPECT_EQ(receiver_config.peer_difference_cnt, 3);

  EXPECT_EQ(sender_config.self_intersection_cnt, 9);
  EXPECT_EQ(sender_config.self_difference_cnt, 3);
  EXPECT_EQ(sender_config.peer_difference_cnt, 4);

  AdvancedJoinGenerateResult(receiver_config);
  AdvancedJoinGenerateResult(sender_config);

  constexpr auto expected_output_content_receiver = R"csv("id1","id2","y1"
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
3,"d","y1_7"
3,"d","y1_8"
3,"d","y1_10"
5,"c","y1_9"
NA,NA,NA
NA,NA,NA
NA,NA,NA
)csv";

  constexpr auto expected_output_content_sender = R"csv("id1","id2","y2"
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
NA,NA,NA
NA,NA,NA
NA,NA,NA
NA,NA,NA
5,"b","y2_10"
5,"b","y2_11"
6,"c","y2_12"
)csv";

  EXPECT_EQ(LoadFile(receiver_config.output_path),
            expected_output_content_receiver);
  EXPECT_EQ(LoadFile(sender_config.output_path),
            expected_output_content_sender);
}

TEST(AdvancedJoinTest, Difference) {
  std::filesystem::path receiver_input_path =
      std::filesystem::temp_directory_path() / "receiver_input.csv";
  std::filesystem::path receiver_output_path =
      std::filesystem::temp_directory_path() / "receiver_difference_output.csv";
  SaveFile(receiver_input_path, kInputContentReceiver);

  std::filesystem::path sender_input_path =
      std::filesystem::temp_directory_path() / "sender_input.csv";
  std::filesystem::path sender_output_path =
      std::filesystem::temp_directory_path() / "sender_difference_output.csv";
  SaveFile(sender_input_path, kInputContentSender);

  AdvancedJoinConfig receiver_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE, v2::ROLE_RECEIVER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      receiver_input_path, receiver_output_path,
      std::filesystem::temp_directory_path());
  AdvancedJoinConfig sender_config = BuildAdvancedJoinConfig(
      v2::PsiConfig::ADVANCED_JOIN_TYPE_DIFFERENCE, v2::ROLE_SENDER,
      v2::ROLE_RECEIVER, std::vector<std::string>{"id1", "id2"},
      sender_input_path, sender_output_path,
      std::filesystem::temp_directory_path());

  AdvancedJoinPreprocess(&receiver_config);
  AdvancedJoinPreprocess(&sender_config);

  SaveFile(receiver_config.self_intersection_cnt_path,
           kPSIOuputContentReceiver);
  SaveFile(sender_config.self_intersection_cnt_path, kPSIOuputContentSender);

  auto lctxs = yacl::link::test::SetupWorld(2);

  auto proc = [&](int idx) {
    if (idx == 1) {
      AdvancedJoinSync(lctxs[idx], &receiver_config);
    } else {
      AdvancedJoinSync(lctxs[idx], &sender_config);
    }
  };

  std::vector<std::future<void>> fs(2);

  for (size_t i = 0; i < 2; i++) {
    fs[i] = std::async(proc, i);
  }

  for (size_t i = 0; i < 2; i++) {
    try {
      fs[i].get();
    } catch (...) {
      std::exception_ptr ep = std::current_exception();
      std::rethrow_exception(ep);
    }
  }

  EXPECT_EQ(receiver_config.self_intersection_cnt, 6);
  EXPECT_EQ(receiver_config.self_difference_cnt, 4);
  EXPECT_EQ(receiver_config.peer_difference_cnt, 3);

  EXPECT_EQ(sender_config.self_intersection_cnt, 9);
  EXPECT_EQ(sender_config.self_difference_cnt, 3);
  EXPECT_EQ(sender_config.peer_difference_cnt, 4);

  AdvancedJoinGenerateResult(receiver_config);
  AdvancedJoinGenerateResult(sender_config);

  constexpr auto expected_output_content_receiver = R"csv("id1","id2","y1"
3,"d","y1_7"
3,"d","y1_8"
3,"d","y1_10"
5,"c","y1_9"
NA,NA,NA
NA,NA,NA
NA,NA,NA
)csv";

  constexpr auto expected_output_content_sender = R"csv("id1","id2","y2"
NA,NA,NA
NA,NA,NA
NA,NA,NA
NA,NA,NA
5,"b","y2_10"
5,"b","y2_11"
6,"c","y2_12"
)csv";

  EXPECT_EQ(LoadFile(receiver_config.output_path),
            expected_output_content_receiver);
  EXPECT_EQ(LoadFile(sender_config.output_path),
            expected_output_content_sender);
}

}  // namespace psi
