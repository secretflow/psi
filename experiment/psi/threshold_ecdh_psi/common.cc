// Copyright 2025
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "experiment/psi/threshold_ecdh_psi/common.h"

#include "spdlog/spdlog.h"

#include "psi/utils/io.h"

namespace psi::ecdh {
void SaveIntersectionCount(const std::string &count_path, uint32_t real_count,
                           uint32_t final_count) {
  auto ofs = io::BuildOutputStream(io::FileIoOptions(count_path));

  ofs->Write("real_count,final_count\n");
  ofs->Write(fmt::format("{},{}\n", real_count, final_count));

  ofs->Close();
  SPDLOG_INFO("Save intersection count to {}", count_path);
}
}  // namespace psi::ecdh