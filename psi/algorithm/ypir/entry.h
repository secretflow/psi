#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "yacl/link/context.h"

#include "psi/algorithm/ypir/types.h"

namespace psi::ypir {

struct YpirSenderOptions {
  YpirMode mode = YpirMode::kSimplepir;
  uint64_t db_rows = 0;
  uint64_t db_cols = 0;
  uint64_t item_size_bits = 0;
  std::string db_file;
};

struct YpirReceiverOptions {
  YpirMode mode = YpirMode::kSimplepir;
  uint64_t db_rows = 0;
  uint64_t db_cols = 0;
  uint64_t item_size_bits = 0;
  std::string query_file;
  std::string output_file;
};

int SenderOnline(const YpirSenderOptions& options,
                 std::shared_ptr<yacl::link::Context> lctx);

int ReceiverOnline(const YpirReceiverOptions& options,
                   std::shared_ptr<yacl::link::Context> lctx);

}  // namespace psi::ypir
