#pragma once

#include <cstddef>
#include <cstdint>

#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/ypir/types.h"

namespace psi::ypir {

struct YpirParameters {
  YpirMode mode = YpirMode::kSimplepir;
  psi::spiral::Params spiral_params;
  uint64_t db_rows = 0;
  uint64_t db_cols = 0;
  size_t value_bytes = 0;

  [[nodiscard]] uint64_t NumItems() const { return db_rows * db_cols; }
};

YpirParameters CreateParamsForScenarioSimplePIR(uint64_t num_items,
                                                uint64_t item_size_bits);
YpirParameters CreateParamsForScenarioDoublePIR(uint64_t num_items,
                                                uint64_t item_size_bits);
YpirParameters CreateParamsForShapeSimplePIR(uint64_t db_rows,
                                             uint64_t db_cols,
                                             uint64_t item_size_bits);
YpirParameters CreateParamsForShapeDoublePIR(uint64_t db_rows,
                                             uint64_t db_cols,
                                             uint64_t item_size_bits);
YpirParameters CreateSmallTestParamsSimplePIR();
YpirParameters CreateSmallTestParamsDoublePIR();

}  // namespace psi::ypir
