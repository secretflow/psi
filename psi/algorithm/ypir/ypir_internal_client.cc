#include "psi/algorithm/ypir/ypir_internal_client.h"

#include "yacl/base/exception.h"

#include "psi/algorithm/ypir/util.h"

namespace psi::ypir::internal::ypir {

YpirQuery GenerateQuery(uint64_t raw_idx, const YpirParameters& params,
                        ClientSecrets& secrets, const Context& context) {
  YACL_ENFORCE(params.mode == YpirMode::kDoublepir);
  YACL_ENFORCE_LT(raw_idx, params.NumItems());

  const uint64_t row_idx = raw_idx / params.db_cols;
  const uint64_t col_idx = raw_idx % params.db_cols;

  secrets.simple_secret =
      psi::ypir::ypir_internal::Secret(context.fhe_params->get_lwe_dimension(),
                                       context.fhe_params->get_lwe_cmod());
  secrets.double_secret =
      psi::ypir::ypir_internal::Secret(context.fhe_params->get_poly_degree(),
                                       context.fhe_params->get_rlwe_cmod());
  secrets.initialized = true;

  auto legacy_query = psi::ypir::ypir_internal::Generate_query_ypir(
      col_idx, row_idx, secrets.simple_secret, secrets.double_secret,
      *context.prng, *context.fhe_params, *context.pir_params);

  YpirQuery query;
  query.mode = YpirMode::kDoublepir;
  query.qu0 = std::move(legacy_query.qu0);
  query.qu1 = std::move(legacy_query.qu1);
  query.ksk_b = std::move(legacy_query.ksk_b);
  return query;
}

std::vector<uint8_t> RecoverResponse(const YpirResponse& response,
                                     const YpirParameters& params,
                                     const ClientSecrets& secrets,
                                     const Context& context) {
  YACL_ENFORCE(params.mode == YpirMode::kDoublepir);
  YACL_ENFORCE(secrets.initialized,
               "GenerateQuery must be called before decode");
  YACL_ENFORCE(response.mode == YpirMode::kDoublepir);

  auto simple_secret = secrets.simple_secret;
  auto double_secret = secrets.double_secret;
  auto result = response.doublepir_response;
  uint64_t message = 0;

  psi::ypir::ypir_internal::YpirRecover(simple_secret, double_secret, result,
                                        message, *context.fhe_params,
                                        *context.pir_params);
  return EncodeIntegerValue(message, params.value_bytes);
}

}  // namespace psi::ypir::internal::ypir
