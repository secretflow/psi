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

#include <cstddef>
#include <utility>
#include <vector>

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/util/delimited_message_util.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/serialize.h"
#include "psi/algorithm/spiral/spiral_client.h"

#include "psi/algorithm/spiral/serializable.pb.h"

namespace psi::spiral {

class SpiralServer : public psi::pir::IndexPirDataBase {
 public:
  explicit SpiralServer(Params params, std::vector<uint64_t> reoriented_db)
      : psi::pir::IndexPirDataBase(psi::pir::PirType::SPIRAL_PIR),
        params_(std::move(params)),
        single_db_size_(reoriented_db.size()),
        reoriented_dbs_(std::move(reoriented_db)) {
    pt_nums_ = params_.NumItems();
    database_seted_ = true;
  }

  explicit SpiralServer(Params params, std::vector<uint64_t> reoriented_db,
                        DatabaseMetaInfo database_info)
      : psi::pir::IndexPirDataBase(psi::pir::PirType::SPIRAL_PIR),
        params_(std::move(params)),
        single_db_size_(reoriented_db.size()),
        reoriented_dbs_(std::move(reoriented_db)),
        database_info_(database_info) {
    pt_nums_ = params_.NumItems();
    database_seted_ = true;
  }

  explicit SpiralServer(Params params, DatabaseMetaInfo database_info)
      : psi::pir::IndexPirDataBase(psi::pir::PirType::SPIRAL_PIR),
        params_(std::move(params)),
        database_info_(database_info) {}

  // convert raw database to Plaintext of SpiralPIR
  void GenerateFromRawData(const psi::pir::RawDatabase& raw_database) override;

  // convert raw database to the specific format required by SpiralPIR
  // now we support value of any length
  void GenerateFromRawDataAndReorient(
      const psi::pir::RawDatabase& raw_database);

  bool DbSeted() const override { return database_seted_; }

  void SetPublicKeys(PublicKeys pks) {
    pks_ = std::move(pks);
    pks_seted_ = true;
  }

  void SetPublicKeys(yacl::Buffer& pks_buffer) {
    pks_ = DeserializePublicKeys(params_, pks_buffer);
    pks_seted_ = true;
  }
  void SetPublicKeys(const std::string& pks_buffer) {
    pks_ = DeserializePublicKeys(params_, pks_buffer);
    pks_seted_ = true;
  }

  std::vector<PolyMatrixRaw> ProcessQuery(const SpiralQuery& query) const;

  std::vector<PolyMatrixRaw> ProcessQuery(const SpiralQuery& query,
                                          const PublicKeys& pks) const;

  const Params& GetParams() const { return params_; }

  yacl::Buffer Response(const yacl::Buffer& query_buffer,
                        const yacl::Buffer& pks_buffer) const override {
    auto query = SpiralQuery::DeserializeRng(params_, query_buffer);
    auto pks = DeserializePublicKeys(params_, pks_buffer);

    yacl::ElapsedTimer timer;

    auto response = ProcessQuery(query, pks);

    SPDLOG_INFO("One index query, time cost: {} ms", timer.CountMs());

    return SerializeResponse(response);
  }
  std::string Response(const std::string& query_buffer,
                       const std::string& pks_buffer) const override {
    auto query = SpiralQuery::DeserializeRng(params_, query_buffer);
    auto pks = DeserializePublicKeys(params_, pks_buffer);

    yacl::ElapsedTimer timer;
    auto response = ProcessQuery(query, pks);
    SPDLOG_INFO("One index query, time cost: {} ms", timer.CountMs());

    return SerializeResponseToStr(response);
  }

  void SetPtNums(size_t pt_nums) { pt_nums_ = pt_nums; }

  void SetSingleDbSize(size_t single_db_size) {
    single_db_size_ = single_db_size;
  }

  void SetSinglePtDbSize(size_t single_pt_db_size) {
    single_pt_db_size_ = single_pt_db_size;
  }

  void SetPartitionNum(size_t partition_num) { partition_num_ = partition_num; }

  SpiralServerProto SerializeToProto() const;
  static std::unique_ptr<SpiralServer> DeserializeFromProto(
      const SpiralServerProto& proto);

  void Dump(std::ostream& output) const override;

  static std::unique_ptr<SpiralServer> Load(
      google::protobuf::io::FileInputStream& input);

 protected:
  std::vector<uint64_t> ReorientRawDb(
      const std::vector<std::vector<uint8_t>>& raw_database);

  // ConvertRawDbToPtDb
  std::vector<uint8_t> ConvertRawDbToPtDb(
      const std::vector<std::vector<uint8_t>>& raw_database);

  // convert pt dbs to reoriented dbs
  void PtDbsToReorientedDbs(const std::vector<uint8_t>& pt_dbs);

  PolyMatrixNtt Pack(const std::vector<PolyMatrixRaw>& v_ct,
                     const std::vector<PolyMatrixNtt>& v_w) const;

  void CoefficientExpansion(std::vector<PolyMatrixNtt>& v, size_t g,
                            size_t stop_round,
                            const std::vector<PolyMatrixNtt>& v_w_left,
                            const std::vector<PolyMatrixNtt>& v_w_right,
                            const std::vector<PolyMatrixNtt>& v_neg1,
                            size_t max_btis_to_gen_right) const;

  std::pair<std::vector<uint64_t>, std::vector<PolyMatrixNtt>> ExpandQuery(
      const SpiralQuery& query) const;

  std::pair<std::vector<uint64_t>, std::vector<PolyMatrixNtt>> ExpandQuery(
      const SpiralQuery& query, const PublicKeys& pks) const;

  void RegevToGsw(std::vector<PolyMatrixNtt>& v_gsw,
                  const std::vector<PolyMatrixNtt>& v_inp,
                  const PolyMatrixNtt& v, size_t idx_factor,
                  size_t idx_offset) const;

  void FoldCiphertexts(std::vector<PolyMatrixRaw>& v_cts,
                       const std::vector<PolyMatrixNtt>& v_folding,
                       const std::vector<PolyMatrixNtt>& v_folding_neg) const;

  void MultiplyRegByDatabase(std::vector<PolyMatrixNtt>& out,
                             std::vector<uint64_t>& v_first_dim, size_t dim0,
                             size_t num_per, size_t cur_db_idx = 0,
                             size_t partition_idx = 0) const;

  std::vector<PolyMatrixNtt> GetVFoldingNeg(
      std::vector<PolyMatrixNtt>& v_folding) const;

  void SetReorientedDbs(std::vector<uint64_t> reoriented_dbs) {
    reoriented_dbs_ = std::move(reoriented_dbs);
    database_seted_ = true;
  }

 private:
  Params params_;

  PublicKeys pks_;

  // pt nums after database processed
  size_t pt_nums_ = 0;

  // single reoriented_db's length
  size_t single_db_size_ = 0;

  // single reoriented_raw_db's length
  // single_pt_db_size_
  size_t single_pt_db_size_ = 0;

  // contains partition_num_  reoriented_raw_db
  // pt_dbs_
  std::vector<uint8_t> pt_dbs_;

  // contains partition_num_  reoriented_db
  std::vector<uint64_t> reoriented_dbs_;

  size_t partition_num_ = 1;

  DatabaseMetaInfo database_info_;

  bool database_seted_ = false;

  bool pks_seted_ = false;
};

// util method
std::vector<PolyMatrixNtt> GetVneg1(const Params& params);

// just for test
std::pair<PolyMatrixRaw, std::vector<uint64_t>> GenRandomDbAndGetItem(
    const Params& params, size_t item_idx);

// database is a 2^{v1 + v2} rows matrix, each row is R_p^{n * n}
// reorient the database format for Spiral requireed
std::vector<uint64_t> ReorientDatabase(
    const Params& params, std::vector<std::vector<uint64_t>>& database);

}  // namespace psi::spiral
