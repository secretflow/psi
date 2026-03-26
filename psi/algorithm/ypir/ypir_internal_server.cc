#include "psi/algorithm/ypir/ypir_internal_server.h"

#include "yacl/base/exception.h"

#include "psi/algorithm/ypir/legacy/server.h"

namespace psi::ypir::internal::ypir {
namespace {

std::vector<std::vector<uint64_t>> ExpandDatabase(
    const std::vector<uint8_t>& db, uint64_t rows, uint64_t cols) {
  YACL_ENFORCE_EQ(db.size(), rows * cols);
  std::vector<std::vector<uint64_t>> out(rows, std::vector<uint64_t>(cols, 0));
  for (uint64_t row = 0; row < rows; ++row) {
    for (uint64_t col = 0; col < cols; ++col) {
      out[row][col] = db[row * cols + col];
    }
  }
  return out;
}

}  // namespace

YpirPrecomputedState PrepareOfflineState(const std::vector<uint8_t>& db,
                                         const YpirParameters& params,
                                         const Context& context) {
  YACL_ENFORCE(params.mode == YpirMode::kDoublepir);
  auto db_matrix = ExpandDatabase(db, params.db_rows, params.db_cols);

  YpirPrecomputedState state;
  state.mode = YpirMode::kDoublepir;
  std::vector<std::vector<uint64_t>> server_hint;
  psi::ypir::ypir_internal::YpirHintGenerate(
      db_matrix, state.hint_0, server_hint, state.decomp_buf, *context.prng,
      *context.fhe_params, *context.pir_params);
  psi::ypir::ypir_internal::MatrixTranspose(server_hint, state.server_hint);
  return state;
}

YpirResponse ProcessQuery(const std::vector<uint8_t>& db,
                          const YpirQuery& query,
                          const YpirPrecomputedState& state,
                          const YpirParameters& params,
                          const Context& context) {
  YACL_ENFORCE(params.mode == YpirMode::kDoublepir);
  YACL_ENFORCE(query.mode == YpirMode::kDoublepir);
  YACL_ENFORCE(state.mode == YpirMode::kDoublepir);

  std::vector<uint32_t> qu0(query.qu0.size(), 0);
  for (size_t i = 0; i < query.qu0.size(); ++i) {
    qu0[i] = static_cast<uint32_t>(query.qu0[i]);
  }

  auto qu1 = query.qu1;
  auto decomp_buf = state.decomp_buf;
  const auto& server_hint = state.server_hint;

  std::vector<std::vector<uint64_t>> result;
  result.push_back(state.hint_0);
  psi::ypir::ypir_internal::YpirAnswer(
      db.data(), qu0.data(), qu1, query.ksk_b, decomp_buf, server_hint, result,
      *context.fhe_params, *context.pir_params);

  YpirResponse response;
  response.mode = YpirMode::kDoublepir;
  response.doublepir_response = std::move(result);
  return response;
}

}  // namespace psi::ypir::internal::ypir
