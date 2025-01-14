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

// 1. "psi/algorithm/pir_interface/pir_db.h"  --->
// "psi/algorithm/pir_interface/pir_db.h"

// 2. "//psi/algorithm/pir_interface:pir_db",  ---->
// "//psi/algorithm/pir_interface:pir_db"

#include "absl/types/span.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/utils/parallel.h"

namespace psi::pir_utils {

RawDatabase RawDatabase::Random(uint64_t rows, uint64_t row_byte_len) {
  std::vector<std::vector<uint8_t>> db;
  db.reserve(rows);
  yacl::crypto::Prg<uint8_t> prg;
  for (size_t i = 0; i < rows; ++i) {
    std::vector<uint8_t> tmp(row_byte_len);
    prg.Fill(absl::MakeSpan(tmp));
    db.push_back(tmp);
  }

  RawDatabase raw_db;
  raw_db.rows_ = rows;
  raw_db.row_byte_len_ = row_byte_len;
  raw_db.db_ = std::move(db);

  return raw_db;
}

std::vector<uint8_t> RawDatabase::Combine(
    const std::vector<std::vector<uint8_t>>& data, size_t row_byte_len) {
  if (data.size() == 1) {
    YACL_ENFORCE_EQ(data[0].size(), row_byte_len);
    return data[0];
  }

  // assert each partition's length is same
  size_t m = data[0].size();
  for (const auto& tmp : data) {
    YACL_ENFORCE_EQ(tmp.size(), m);
  }

  YACL_ENFORCE_GE(data.size() * m, row_byte_len);

  std::vector<uint8_t> result;
  result.reserve(row_byte_len);

  for (size_t i = 0; i < data.size(); ++i) {
    if (i < data.size() - 1) {
      // just insert
      result.insert(result.end(), data[i].begin(), data[i].end());
    } else {
      size_t end = row_byte_len - i * m;
      result.insert(result.end(), data[i].begin(), data[i].begin() + end);
    }
  }
  return result;
}

std::vector<RawDatabase> RawDatabase::Partition(
    size_t partition_byte_len) const {
  // row_byte_len_ >= partition_byte_len
  YACL_ENFORCE_GE(row_byte_len_, partition_byte_len);

  size_t partition_num =
      (row_byte_len_ + partition_byte_len - 1) / partition_byte_len;
  if (partition_num == 1) {
    return std::vector<RawDatabase>{RawDatabase(rows_, row_byte_len_, db_)};
  }

  std::vector<RawDatabase> result;
  result.reserve(partition_num);

  // first process the [0, partition_num - 2]
  for (size_t i = 0; i < partition_num - 1; ++i) {
    size_t start_col = i * partition_byte_len;
    // end_col should <= row_byet_len_
    size_t end_col = start_col + partition_byte_len;

    std::vector<std::vector<uint8_t>> sub_db;
    sub_db.reserve(rows_);
    for (size_t n = 0; n < rows_; ++n) {
      std::vector<uint8_t> chunk(db_[n].begin() + start_col,
                                 db_[n].begin() + end_col);
      sub_db.push_back(std::move(chunk));
    }
    // construct a RawDatabase
    result.emplace_back(rows_, partition_byte_len, std::move(sub_db));
  }
  // then handle the last partiiton
  size_t start_col = (partition_num - 1) * partition_byte_len;
  size_t end_col = std::min(start_col + partition_byte_len, row_byte_len_);
  std::vector<std::vector<uint8_t>> sub_db;
  sub_db.reserve(rows_);
  for (size_t n = 0; n < rows_; ++n) {
    // default padding zeros
    std::vector<uint8_t> chunk(partition_byte_len, 0);
    std::copy(db_[n].begin() + start_col, db_[n].begin() + end_col,
              chunk.begin());
    sub_db.push_back(std::move(chunk));
  }
  result.emplace_back(rows_, partition_byte_len, std::move(sub_db));

  return result;
}

}  // namespace psi::pir_utils
