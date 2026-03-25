#pragma once

#include <cstdint>
#include <vector>

namespace psi::ypir {

enum class YpirMode : uint8_t {
  kSimplepir = 0,
  kDoublepir = 1,
};

struct YpirPrecomputedState {
  YpirMode mode = YpirMode::kSimplepir;
  std::vector<uint64_t> hint_0;
  std::vector<std::vector<uint64_t>> server_hint;
  std::vector<std::vector<std::vector<uint64_t>>> decomp_buf;
};

struct YpirQuery {
  YpirMode mode = YpirMode::kSimplepir;
  std::vector<uint64_t> packed_query_row;
  std::vector<uint64_t> qu0;
  std::vector<uint64_t> qu1;
  std::vector<std::vector<std::vector<uint64_t>>> ksk_b;
};

struct YpirResponse {
  YpirMode mode = YpirMode::kSimplepir;
  std::vector<uint64_t> simplepir_response;
  std::vector<std::vector<uint64_t>> doublepir_response;
};

}  // namespace psi::ypir
