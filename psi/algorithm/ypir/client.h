#pragma once

#include <memory>
#include <string>
#include <vector>

#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"

#include "psi/algorithm/pir_interface/index_pir.h"
#include "psi/algorithm/ypir/ypir_internal_client.h"
#include "psi/algorithm/ypir/params.h"
#include "psi/algorithm/ypir/types.h"

namespace psi::ypir {

class YpirClient : public psi::pir::IndexPirClient {
 public:
  explicit YpirClient(YpirParameters params);

  const YpirParameters& GetParameters() const { return params_; }

  pir::PirType GetPirType() const override { return pir::PirType::YPIR_PIR; }

  yacl::Buffer GeneratePksBuffer() const override;
  std::string GeneratePksString() const override;

  YpirQuery GenerateQuery(uint64_t raw_idx) const;
  yacl::Buffer GenerateQueryBuffer(uint64_t raw_idx) const;
  yacl::Buffer GenerateIndexQuery(uint64_t raw_idx) const override;
  std::string GenerateIndexQueryStr(uint64_t raw_idx) const override;

  std::vector<uint8_t> DecodeResponse(const YpirResponse& response,
                                      uint64_t raw_idx) const;
  std::vector<uint8_t> DecodeResponseBuffer(
      const yacl::ByteContainerView& response_buffer, uint64_t raw_idx) const;
  std::vector<uint8_t> DecodeIndexResponse(
      const yacl::ByteContainerView& response_buffer,
      uint64_t raw_idx) const override;

 private:
  YpirParameters params_;
  mutable std::unique_ptr<internal::ypir::Context> ypir_context_;
  mutable internal::ypir::ClientSecrets client_secrets_;
};

}  // namespace psi::ypir
