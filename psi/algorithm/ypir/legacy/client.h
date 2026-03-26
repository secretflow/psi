#pragma once

#include <cstdint>
#include <vector>

#include "psi/algorithm/ypir/legacy/ypir_params.h"

namespace psi::ypir::ypir_internal {

struct YpirQuery {
  std::vector<uint64_t> qu0;
  std::vector<uint64_t> qu1;
  std::vector<std::vector<std::vector<uint64_t>>> ksk_b;
};

void YpirRecover(Secret& simple_sk, Secret& double_sk,
                 std::vector<std::vector<uint64_t>>& res, uint64_t& message,
                 const FheParams& fparm, const PirParams& pparm);

YpirQuery Generate_query_ypir(uint64_t c_idx, uint64_t r_idx, Secret& lwe_sk,
                              Secret& rlwe_sk, AESCTR_PRNG& prng,
                              const FheParams& fparm, const PirParams& pparm);

}  // namespace psi::ypir::ypir_internal
