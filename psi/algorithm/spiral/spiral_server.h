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

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/serialize.h"
#include "psi/algorithm/spiral/spiral_client.h"

namespace psi::spiral {

class SpiralServer : psi::pir::IndexPirServer {
 public:
  SpiralServer() = default;

  explicit SpiralServer(Params params, std::vector<uint64_t> reoriented_db)
      : params_(std::move(params)),
        single_db_size_(reoriented_db.size()),
        reoriented_dbs_(std::move(reoriented_db)) {
    database_seted_ = true;
  }

  explicit SpiralServer(Params params, std::vector<uint64_t> reoriented_db,
                        DatabaseMetaInfo database_info)
      : params_(std::move(params)),
        single_db_size_(reoriented_db.size()),
        reoriented_dbs_(std::move(reoriented_db)),
        database_info_(database_info) {
    database_seted_ = true;
  }

  explicit SpiralServer(Params params, DatabaseMetaInfo database_info)
      : params_(std::move(params)), database_info_(database_info) {}

  // convert raw database to the specific format required by SpiralPIR
  // now we support value of any length
  void SetDatabase(const pir_utils::RawDatabase& raw_database) override;

  void SetDatabase(std::vector<std::vector<uint8_t>> raw_database) {
    pir_utils::RawDatabase raw_db(std::move(raw_database));
    SetDatabase(raw_db);
  }

  void SetPublicKeys(PublicKeys pks) { pks_ = std::move(pks); }

  void SetPublicKeys(yacl::Buffer& pks_buffer) {
    pks_ = DeserializePublicKeys(params_, pks_buffer);
  }

  std::vector<PolyMatrixRaw> ProcessQuery(const SpiralQuery& query) const;

  const Params& GetParams() const { return params_; }

  virtual yacl::Buffer GenerateIndexResponse(
      const yacl::Buffer& query_buffer) const override {
    auto query = SpiralQuery::DeserializeRng(params_, query_buffer);

    auto response = ProcessQuery(query);

    return SerializeResponse(response);
  }

 protected:
  std::vector<uint64_t> ReorientRawDb(
      const std::vector<std::vector<uint8_t>>& raw_database);

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

 private:
  Params params_;

  PublicKeys pks_;

  // single reoriented_db's length
  size_t single_db_size_ = 0;

  // contains partition_num_  reoriented_db
  std::vector<uint64_t> reoriented_dbs_;

  size_t partition_num_ = 1;

  DatabaseMetaInfo database_info_;

  bool database_seted_ = false;
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