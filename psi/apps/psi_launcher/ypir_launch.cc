#include "psi/apps/psi_launcher/ypir_launch.h"

#include "psi/algorithm/ypir/entry.h"
#include "yacl/base/exception.h"

namespace psi {
namespace {

psi::ypir::YpirMode ToYpirMode(YpirProtocolMode mode) {
  switch (mode) {
    case YPIR_PROTOCOL_MODE_SIMPLEPIR:
      return psi::ypir::YpirMode::kSimplepir;
    case YPIR_PROTOCOL_MODE_DOUBLEPIR:
      return psi::ypir::YpirMode::kDoublepir;
    default:
      YACL_THROW("unsupported YPIR mode");
  }
}

}  // namespace

PirResultReport RunPir(const YpirReceiverConfig& ypir_receiver_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::ypir::YpirReceiverOptions options;
  options.mode = ToYpirMode(ypir_receiver_config.mode());
  options.db_rows = ypir_receiver_config.db_rows();
  options.db_cols = ypir_receiver_config.db_cols();
  options.item_size_bits = ypir_receiver_config.item_size_bits();
  options.query_file = ypir_receiver_config.query_file();
  options.output_file = ypir_receiver_config.output_file();

  YACL_ENFORCE_EQ(psi::ypir::ReceiverOnline(options, lctx), 0);
  return PirResultReport();
}

PirResultReport RunPir(const YpirSenderConfig& ypir_sender_config,
                       const std::shared_ptr<yacl::link::Context>& lctx) {
  psi::ypir::YpirSenderOptions options;
  options.mode = ToYpirMode(ypir_sender_config.mode());
  options.db_rows = ypir_sender_config.db_rows();
  options.db_cols = ypir_sender_config.db_cols();
  options.item_size_bits = ypir_sender_config.item_size_bits();
  options.db_file = ypir_sender_config.db_file();

  YACL_ENFORCE_EQ(psi::ypir::SenderOnline(options, lctx), 0);
  return PirResultReport();
}

}  // namespace psi
