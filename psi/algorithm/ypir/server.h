#pragma once

#include <cstdint>
#include <vector>

#include "psi/algorithm/ypir/ypir_params.h"

namespace psi::ypir::byhe {

void YpirAnswer(const uint8_t* db, uint32_t* qu0, std::vector<uint64_t>& qu1,
                const std::vector<std::vector<std::vector<uint64_t>>>& ksk_b,
                std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
                std::vector<std::vector<uint64_t>>& server_hint,
                std::vector<std::vector<uint64_t>>& res,
                const FheParams& fparm, const PirParams& pparm);

void YpirHintGenerate(std::vector<std::vector<uint64_t>>& db,
                      std::vector<uint64_t>& H0,
                      std::vector<std::vector<uint64_t>>& double_server_hint,
                      std::vector<std::vector<std::vector<uint64_t>>>& decomp_buf,
                      AESCTR_PRNG& prng,
                      const FheParams& fparm, const PirParams& pparm);

}  // namespace psi::ypir::byhe
