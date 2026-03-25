#include "psi/algorithm/ypir/client.h"

#include <string>
#include <utility>

#include "yacl/base/exception.h"

#include "psi/algorithm/ypir/serialize.h"
#include "psi/algorithm/ypir/util.h"

namespace psi::ypir {

YpirClient::YpirClient(YpirParameters params) : params_(std::move(params)) {
  if (params_.mode == YpirMode::kDoublepir) {
    ypir_context_ = std::make_unique<internal::ypir::Context>(
        internal::ypir::CreateContext(params_));
  }
}

yacl::Buffer YpirClient::GeneratePksBuffer() const { return yacl::Buffer(); }

std::string YpirClient::GeneratePksString() const { return {}; }

YpirQuery YpirClient::GenerateQuery(uint64_t raw_idx) const {
  YACL_ENFORCE_LT(raw_idx, params_.NumItems());

  if (params_.mode == YpirMode::kSimplepir) {
    YpirQuery query;
    query.mode = YpirMode::kSimplepir;
    query.packed_query_row = BuildPackedSimplepirQuery(params_, raw_idx);
    return query;
  }

  YACL_ENFORCE(ypir_context_ != nullptr);
  return internal::ypir::GenerateQuery(raw_idx, params_, client_secrets_,
                                       *ypir_context_);
}

yacl::Buffer YpirClient::GenerateQueryBuffer(uint64_t raw_idx) const {
  return SerializeQuery(GenerateQuery(raw_idx));
}

yacl::Buffer YpirClient::GenerateIndexQuery(uint64_t raw_idx) const {
  return GenerateQueryBuffer(raw_idx);
}

std::string YpirClient::GenerateIndexQueryStr(uint64_t raw_idx) const {
  auto buffer = GenerateQueryBuffer(raw_idx);
  return std::string(static_cast<std::string_view>(buffer));
}

std::vector<uint8_t> YpirClient::DecodeResponse(const YpirResponse& response,
                                                uint64_t raw_idx) const {
  YACL_ENFORCE_LT(raw_idx, params_.NumItems());
  YACL_ENFORCE(response.mode == params_.mode);

  if (params_.mode == YpirMode::kSimplepir) {
    YACL_ENFORCE_EQ(response.simplepir_response.size(), params_.db_cols);
    const uint64_t col_idx = raw_idx % params_.db_cols;
    const uint64_t decoded =
        DecodeSimplepirValue(params_, response.simplepir_response[col_idx]);
    return EncodeIntegerValue(decoded, params_.value_bytes);
  }

  YACL_ENFORCE(ypir_context_ != nullptr);
  return internal::ypir::RecoverResponse(response, params_, client_secrets_,
                                         *ypir_context_);
}

std::vector<uint8_t> YpirClient::DecodeResponseBuffer(
    const yacl::ByteContainerView& response_buffer, uint64_t raw_idx) const {
  return DecodeResponse(DeserializeResponse(response_buffer), raw_idx);
}

std::vector<uint8_t> YpirClient::DecodeIndexResponse(
    const yacl::ByteContainerView& response_buffer, uint64_t raw_idx) const {
  return DecodeResponseBuffer(response_buffer, raw_idx);
}

}  // namespace psi::ypir
