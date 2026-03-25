#include "psi/algorithm/ypir/server.h"

#include <cstring>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "yacl/base/exception.h"

#include "psi/algorithm/ypir/legacy/util.h"
#include "psi/algorithm/ypir/serialize.h"

namespace psi::ypir {
namespace {

template <typename T>
std::vector<T> RowBytesToValues(const std::vector<uint8_t>& bytes) {
  YACL_ENFORCE_EQ(bytes.size() % sizeof(T), 0U);
  std::vector<T> out(bytes.size() / sizeof(T));
  std::memcpy(out.data(), bytes.data(), bytes.size());
  return out;
}

template <typename T>
T ReadScalarValue(const std::vector<uint8_t>& bytes) {
  YACL_ENFORCE_EQ(bytes.size(), sizeof(T));
  T out = 0;
  std::memcpy(&out, bytes.data(), sizeof(T));
  return out;
}

}  // namespace

template <typename T>
YpirServer<T>::YpirServer(YpirParameters params)
    : psi::pir::IndexPirDataBase(psi::pir::PirType::YPIR_PIR),
      params_(std::move(params)) {
  if (params_.mode == YpirMode::kDoublepir) {
    ypir_context_ = std::make_unique<internal::ypir::Context>(
        internal::ypir::CreateContext(params_));
  }
}

template <typename T>
void YpirServer<T>::GenerateFromRawData(const psi::pir::RawDatabase& raw_database) {
  static_assert(std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t>,
                "YpirServer only supports uint8_t and uint16_t values");

  const bool item_layout =
      raw_database.Rows() <= params_.NumItems() &&
      raw_database.RowByteLen() == params_.value_bytes;
  const bool matrix_layout =
      raw_database.Rows() == params_.db_rows &&
      raw_database.RowByteLen() == params_.db_cols * sizeof(T);
  YACL_ENFORCE(item_layout || matrix_layout,
               "raw database shape does not match YPIR parameters");

  if (params_.mode == YpirMode::kSimplepir) {
    simplepir_db_column_major_.assign(params_.db_rows * params_.db_cols, 0);
    if (item_layout) {
      YACL_ENFORCE_EQ(params_.value_bytes, sizeof(T));
      for (uint64_t raw_idx = 0; raw_idx < raw_database.Rows(); ++raw_idx) {
        const uint64_t row = raw_idx / params_.db_cols;
        const uint64_t col = raw_idx % params_.db_cols;
        simplepir_db_column_major_[col * params_.db_rows + row] =
            ReadScalarValue<T>(raw_database.At(raw_idx));
      }
    } else {
      for (uint64_t row = 0; row < params_.db_rows; ++row) {
        auto row_values = RowBytesToValues<T>(raw_database.At(row));
        for (uint64_t col = 0; col < params_.db_cols; ++col) {
          simplepir_db_column_major_[col * params_.db_rows + row] =
              row_values[col];
        }
      }
    }
  } else {
    YACL_ENFORCE(sizeof(T) == 1, "DoublePIR currently expects uint8_t rows");
    doublepir_db_row_major_.assign(params_.db_rows * params_.db_cols, 0);
    if (item_layout) {
      YACL_ENFORCE_EQ(params_.value_bytes, sizeof(uint8_t));
      for (uint64_t raw_idx = 0; raw_idx < raw_database.Rows(); ++raw_idx) {
        doublepir_db_row_major_[raw_idx] =
            ReadScalarValue<uint8_t>(raw_database.At(raw_idx));
      }
    } else {
      for (uint64_t row = 0; row < params_.db_rows; ++row) {
        const auto& row_bytes = raw_database.At(row);
        std::memcpy(doublepir_db_row_major_.data() + row * params_.db_cols,
                    row_bytes.data(), row_bytes.size());
      }
    }
  }

  db_set_ = true;
}

template <typename T>
void YpirServer<T>::GenerateFromSimpleHashTable(
    const psi::pir::RawDatabase& raw_database) {
  GenerateFromRawData(raw_database);
}

template <typename T>
void YpirServer<T>::Dump(std::ostream& out_stream) const {
  out_stream << "YpirServer{mode="
             << (params_.mode == YpirMode::kSimplepir ? "simplepir"
                                                      : "doublepir")
             << ", db_rows=" << params_.db_rows << ", db_cols="
             << params_.db_cols << ", value_bytes=" << params_.value_bytes
             << ", db_set=" << db_set_ << "}";
}

template <typename T>
YpirPrecomputedState YpirServer<T>::PerformOfflinePrecomputation() const {
  YACL_ENFORCE(db_set_, "database must be loaded before precomputation");

  if (params_.mode == YpirMode::kSimplepir) {
    YpirPrecomputedState state;
    state.mode = YpirMode::kSimplepir;
    return state;
  }

  YACL_ENFORCE(ypir_context_ != nullptr);
  return internal::ypir::PrepareOfflineState(doublepir_db_row_major_, params_,
                                             *ypir_context_);
}

template <typename T>
YpirResponse YpirServer<T>::ProcessQuery(const YpirQuery& query) const {
  return ProcessQuery(query, PerformOfflinePrecomputation());
}

template <typename T>
YpirResponse YpirServer<T>::ProcessQuery(const YpirQuery& query,
                                         const YpirPrecomputedState& state) const {
  YACL_ENFORCE(db_set_, "database must be loaded before query processing");
  YACL_ENFORCE(query.mode == params_.mode);

  if (params_.mode == YpirMode::kSimplepir) {
    YACL_ENFORCE_EQ(query.packed_query_row.size(), params_.db_rows);

    std::vector<uint64_t> result(params_.db_cols, 0);
    psi::ypir::FastBatchedDotProduct<T>(
        params_.spiral_params, result.data(), query.packed_query_row.data(),
        params_.db_rows, simplepir_db_column_major_.data(), params_.db_rows,
        params_.db_cols);

    YpirResponse response;
    response.mode = YpirMode::kSimplepir;
    response.simplepir_response = std::move(result);
    return response;
  }

  YACL_ENFORCE(ypir_context_ != nullptr);
  return internal::ypir::ProcessQuery(doublepir_db_row_major_, query, state,
                                      params_, *ypir_context_);
}

template <typename T>
yacl::Buffer YpirServer<T>::Response(
    const yacl::ByteContainerView& query_buffer) const {
  return SerializeResponse(ProcessQuery(DeserializeQuery(query_buffer)));
}

template <typename T>
yacl::Buffer YpirServer<T>::Response(
    const yacl::ByteContainerView& query_buffer,
    const yacl::Buffer& /*pks_buffer*/) const {
  return Response(query_buffer);
}

template <typename T>
std::string YpirServer<T>::Response(
    const yacl::ByteContainerView& query_buffer,
    const std::string& /*pks_buffer*/) const {
  auto buffer = Response(query_buffer);
  return std::string(static_cast<std::string_view>(buffer));
}

template class YpirServer<uint8_t>;
template class YpirServer<uint16_t>;

}  // namespace psi::ypir
