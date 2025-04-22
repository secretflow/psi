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

#pragma once
#include <cstdint>
#include <vector>

#include "fmt/format.h"
#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"
#include "yacl/base/exception.h"
#include "yacl/base/int128.h"

#include "psi/algorithm/pir_interface/pir_type.pb.h"

namespace {

// ensure each row byte len is same
void ValidateDb(const std::vector<std::vector<uint8_t>>& db) {
  size_t first_row_len = db[0].size();
  for (const auto& inner : db) {
    YACL_ENFORCE_EQ(first_row_len, inner.size());
  }
}

void ValidateDb(const std::vector<std::vector<uint8_t>>& db,
                size_t target_len) {
  for (const auto& inner : db) {
    YACL_ENFORCE_EQ(target_len, inner.size());
  }
}

}  // namespace

namespace psi::pir {
// PirTye was defined in the pir_type.pb.h
PirTypeProto PirTypeToProto(const PirType& type);

PirType ProtoToPirType(const PirTypeProto& proto);

// forward declare
class RawDatabase;

class KwPirDataBase {
 public:
  KwPirDataBase() = default;
  virtual ~KwPirDataBase() = default;

  // if our setting is no payload, we need to use this function
  // to init our DB
  virtual void GenerateFromRawKeyData(
      const std::vector<yacl::ByteContainerView>& db_vec) = 0;

  // each element is key & value, each value`s length must be same
  virtual void GenerateFromRawKeyValueData(
      const std::vector<std::pair<yacl::ByteContainerView,
                                  yacl::ByteContainerView>>& db_vec) = 0;

  virtual void GenerateFromRawKeyValueData(
      const std::vector<std::string>& keys,
      const std::vector<std::string>& values) = 0;

  virtual void GenerateFromRawKeyValueData(
      const std::vector<uint128_t>& keys,
      const std::vector<std::string>& values) = 0;

  virtual void Dump(std::ostream& out_stream) const = 0;

  virtual PirType GetPirType() const = 0;

  virtual std::vector<yacl::Buffer> Response(
      const std::vector<yacl::Buffer>& query_vec,
      const yacl::Buffer& pks) const = 0;

  virtual std::vector<std::string> Response(
      const std::vector<std::string>& query_vec,
      const std::string& pks) const = 0;

  virtual yacl::Buffer Response(const yacl::Buffer& query_buffer,
                                const yacl::Buffer& pks_buffer) const = 0;

  virtual std::string Response(const std::string& query_buffer,
                               const std::string& pks_buffer) const = 0;
};

class IndexPirDataBase {
 public:
  IndexPirDataBase(PirType pir_type) : pir_type_(pir_type) {}
  virtual ~IndexPirDataBase() = default;

  virtual void GenerateFromRawData(const RawDatabase& raw_data) = 0;

  virtual void Dump(std::ostream& out_stream) const = 0;

  PirType GetPirType() const { return pir_type_; };

  virtual bool DbSeted() const = 0;

  virtual yacl::Buffer Response(const yacl::Buffer& query_buffer,
                                const yacl::Buffer& pks_buffer) const = 0;
  virtual std::string Response(const std::string& query_buffer,
                               const std::string& pks_buffer) const = 0;

 protected:
  PirType pir_type_;
};

// Raw datbase, n * l , n is the rows, l is the byte len of each row
class RawDatabase {
 public:
  RawDatabase() = default;

  RawDatabase(std::vector<std::vector<uint8_t>> db)
      : rows_(db.size()), row_byte_len_(db[0].size()), db_(std::move(db)) {
    ValidateDb(Db());
  }
  RawDatabase(size_t rows, size_t row_byte_len,
              std::vector<std::vector<uint8_t>> db)
      : rows_(rows), row_byte_len_(row_byte_len), db_(std::move(db)) {
    ValidateDb(Db(), RowByteLen());
  }

  static RawDatabase Random(uint64_t rows, uint64_t row_byte_len);

  static std::vector<uint8_t> Combine(
      const std::vector<std::vector<uint8_t>>& data, size_t row_byte_len);

  size_t Rows() const { return rows_; }

  size_t RowByteLen() const { return row_byte_len_; }

  const std::vector<std::vector<uint8_t>>& Db() const { return db_; }

  const std::vector<uint8_t>& At(size_t i) const { return db_.at(i); }

  // Partition the database to some sub-database, for exapmle:
  // the RawDatabase is 100 rows, 64-byte, the  partition_byte_len = 16-byte
  // the partition result is 4 sub-databases, each sub database is 100 rows,
  // 16-byte
  std::vector<RawDatabase> Partition(size_t partition_byte_len) const;

 private:
  size_t rows_ = 0;

  size_t row_byte_len_ = 0;

  std::vector<std::vector<uint8_t>> db_;
};
}  // namespace psi::pir
