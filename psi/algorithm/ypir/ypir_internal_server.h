#pragma once

#include <cstdint>
#include <vector>

#include "psi/algorithm/ypir/ypir_internal_params.h"
#include "psi/algorithm/ypir/types.h"

namespace psi::ypir::internal::ypir {

YpirPrecomputedState PrepareOfflineState(const std::vector<uint8_t>& db,
                                         const YpirParameters& params,
                                         const Context& context);

YpirResponse ProcessQuery(const std::vector<uint8_t>& db,
                          const YpirQuery& query,
                          const YpirPrecomputedState& state,
                          const YpirParameters& params,
                          const Context& context);

}  // namespace psi::ypir::internal::ypir
