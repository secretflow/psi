#pragma once

#include <memory>

#include "yacl/link/context.h"

#include "psi/proto/pir.pb.h"

namespace psi {

PirResultReport RunPir(const YpirReceiverConfig& ypir_receiver_config,
                       const std::shared_ptr<yacl::link::Context>& lctx);

PirResultReport RunPir(const YpirSenderConfig& ypir_sender_config,
                       const std::shared_ptr<yacl::link::Context>& lctx);

}  // namespace psi
