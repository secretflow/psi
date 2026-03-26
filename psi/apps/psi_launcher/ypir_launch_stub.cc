#include "yacl/base/exception.h"

#include "psi/apps/psi_launcher/ypir_launch.h"

namespace psi {

PirResultReport RunPir(const YpirReceiverConfig&,
                       const std::shared_ptr<yacl::link::Context>&) {
  YACL_THROW("YPIR is only supported on x86_64");
}

PirResultReport RunPir(const YpirSenderConfig&,
                       const std::shared_ptr<yacl::link::Context>&) {
  YACL_THROW("YPIR is only supported on x86_64");
}

}  // namespace psi
