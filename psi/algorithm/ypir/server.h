#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"

#include "psi/algorithm/pir_interface/pir_db.h"
#include "psi/algorithm/ypir/params.h"
#include "psi/algorithm/ypir/types.h"
#include "psi/algorithm/ypir/ypir_internal_server.h"

namespace psi::ypir {

template <typename T>
class YpirServer : public psi::pir::IndexPirDataBase {
 public:
  explicit YpirServer(YpirParameters params);

  void GenerateFromRawData(const psi::pir::RawDatabase& raw_database) override;
  void GenerateFromSimpleHashTable(
      const psi::pir::RawDatabase& raw_database) override;
  void Dump(std::ostream& out_stream) const override;
  std::size_t MaxElementsOfOnePt() const override { return 1; }
  bool DbSeted() const override { return db_set_; }

  [[nodiscard]] bool DbSet() const { return db_set_; }
  [[nodiscard]] const YpirParameters& GetParameters() const { return params_; }

  YpirPrecomputedState PerformOfflinePrecomputation() const;
  YpirResponse ProcessQuery(const YpirQuery& query) const;
  YpirResponse ProcessQuery(const YpirQuery& query,
                            const YpirPrecomputedState& state) const;
  yacl::Buffer Response(const yacl::ByteContainerView& query_buffer) const;
  yacl::Buffer Response(const yacl::ByteContainerView& query_buffer,
                        const yacl::Buffer& pks_buffer) const override;
  std::string Response(const yacl::ByteContainerView& query_buffer,
                       const std::string& pks_buffer) const override;

 private:
  YpirParameters params_;
  bool db_set_ = false;
  std::vector<T> simplepir_db_column_major_;
  std::vector<uint8_t> doublepir_db_row_major_;
  std::unique_ptr<internal::ypir::Context> ypir_context_;
};

extern template class YpirServer<uint8_t>;
extern template class YpirServer<uint16_t>;

}  // namespace psi::ypir
