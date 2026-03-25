#include "psi/algorithm/ypir/util.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "yacl/base/exception.h"

#include "psi/algorithm/spiral/arith/arith.h"

namespace psi::ypir {

std::vector<uint64_t> BuildPackedSimplepirQuery(const YpirParameters& params,
                                                uint64_t raw_idx) {
  YACL_ENFORCE(params.mode == YpirMode::kSimplepir);
  YACL_ENFORCE_LT(raw_idx, params.NumItems());

  const uint64_t row_idx = raw_idx / params.db_cols;
  std::vector<uint64_t> query(params.db_rows, 0);
  query[row_idx] = params.spiral_params.ScaleK();

  const uint64_t m0 = params.spiral_params.Moduli(0);
  const uint64_t m1 = params.spiral_params.Moduli(1);
  std::vector<uint64_t> packed(params.db_rows, 0);
  for (size_t i = 0; i < packed.size(); ++i) {
    packed[i] = (query[i] % m0) | ((query[i] % m1) << 32);
  }
  return packed;
}

std::vector<uint8_t> EncodeIntegerValue(uint64_t value, size_t value_bytes) {
  std::vector<uint8_t> out(value_bytes, 0);
  for (size_t i = 0; i < value_bytes; ++i) {
    out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
  }
  return out;
}

uint64_t DecodeSimplepirValue(const YpirParameters& params, uint64_t value) {
  return psi::spiral::arith::Rescale(value, params.spiral_params.Modulus(),
                                     params.spiral_params.PtModulus());
}

}  // namespace psi::ypir
