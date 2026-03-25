#pragma once

#include "yacl/base/buffer.h"
#include "yacl/base/byte_container_view.h"

#include "psi/algorithm/ypir/types.h"

namespace psi::ypir {

yacl::Buffer SerializeQuery(const YpirQuery& query);
YpirQuery DeserializeQuery(const yacl::ByteContainerView& buffer);

yacl::Buffer SerializeResponse(const YpirResponse& response);
YpirResponse DeserializeResponse(const yacl::ByteContainerView& buffer);

}  // namespace psi::ypir
