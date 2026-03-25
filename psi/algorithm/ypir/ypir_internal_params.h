#pragma once

#include <memory>

#include "psi/algorithm/ypir/legacy/ypir_params.h"
#include "psi/algorithm/ypir/params.h"

namespace psi::ypir::internal::ypir {

struct Context {
  std::shared_ptr<psi::ypir::ypir_internal::FheParams> fhe_params;
  std::shared_ptr<psi::ypir::ypir_internal::PirParams> pir_params;
  std::shared_ptr<psi::ypir::ypir_internal::AESCTR_PRNG> prng;
};

Context CreateContext(const YpirParameters& params);

}  // namespace psi::ypir::internal::ypir
