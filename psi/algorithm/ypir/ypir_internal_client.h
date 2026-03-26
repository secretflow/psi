#pragma once

#include <vector>

#include "psi/algorithm/ypir/legacy/client.h"
#include "psi/algorithm/ypir/types.h"
#include "psi/algorithm/ypir/ypir_internal_params.h"

namespace psi::ypir::internal::ypir {

struct ClientSecrets {
  psi::ypir::ypir_internal::Secret simple_secret;
  psi::ypir::ypir_internal::Secret double_secret;
  bool initialized = false;
};

YpirQuery GenerateQuery(uint64_t raw_idx, const YpirParameters& params,
                        ClientSecrets& secrets, const Context& context);

std::vector<uint8_t> RecoverResponse(const YpirResponse& response,
                                     const YpirParameters& params,
                                     const ClientSecrets& secrets,
                                     const Context& context);

}  // namespace psi::ypir::internal::ypir
