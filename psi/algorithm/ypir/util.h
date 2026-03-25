#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "psi/algorithm/ypir/params.h"

namespace psi::ypir {

std::vector<uint64_t> BuildPackedSimplepirQuery(const YpirParameters& params,
                                                uint64_t raw_idx);
std::vector<uint8_t> EncodeIntegerValue(uint64_t value, size_t value_bytes);
uint64_t DecodeSimplepirValue(const YpirParameters& params, uint64_t value);

}  // namespace psi::ypir
