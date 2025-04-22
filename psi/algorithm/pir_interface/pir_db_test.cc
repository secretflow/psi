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

#include "psi/algorithm/pir_interface/pir_db.h"

#include <random>

#include "gtest/gtest.h"

namespace psi::pir {

TEST(RawDatabase, Work) {
  RawDatabase db = RawDatabase::Random(100, 256);

  RawDatabase db2 = RawDatabase::Random(1000000, 256);

  RawDatabase db3 = RawDatabase::Random(1ULL << 20, 256);
}

TEST(RawDatabase, Partition) {
  size_t row_byte_len = 256;
  size_t rows = 100000;
  RawDatabase db = RawDatabase::Random(rows, row_byte_len);

  size_t partition_byte_len = 64;
  auto sub_dbs = db.Partition(partition_byte_len);
  ASSERT_EQ(sub_dbs.size(), row_byte_len / partition_byte_len);

  partition_byte_len = 65;
  sub_dbs = db.Partition(partition_byte_len);
  ASSERT_EQ(sub_dbs.size(),
            (row_byte_len + partition_byte_len - 1) / partition_byte_len);

  partition_byte_len = 257;
  ASSERT_THROW(db.Partition(partition_byte_len), yacl::EnforceNotMet);

  // large num 100-kb
  row_byte_len = 102400;
  rows = 10000;
  db = RawDatabase::Random(rows, row_byte_len);

  partition_byte_len = 10240;
  sub_dbs = db.Partition(partition_byte_len);
  ASSERT_EQ(sub_dbs.size(), row_byte_len / partition_byte_len);
}

TEST(RawDatabase, Combine) {
  size_t row_byte_len = 257;
  size_t rows = 100000;
  RawDatabase db = RawDatabase::Random(rows, row_byte_len);

  size_t partition_byte_len = 64;
  auto sub_dbs = db.Partition(partition_byte_len);

  size_t partition_num =
      (row_byte_len + partition_byte_len - 1) / partition_byte_len;

  std::random_device rd;
  std::mt19937_64 rng(rd());

  size_t idx = rng() % rows;
  std::vector<std::vector<uint8_t>> subs;
  for (size_t j = 0; j < partition_num; ++j) {
    subs.push_back(sub_dbs[j].At(idx));
  }
  // combine
  std::vector<uint8_t> combine = RawDatabase::Combine(subs, row_byte_len);
  ASSERT_EQ(combine, db.At(idx));
}

}  // namespace psi::pir