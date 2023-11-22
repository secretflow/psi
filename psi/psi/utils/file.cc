// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/psi/utils/file.h"

#include <filesystem>

#include "yacl/base/exception.h"

namespace psi::psi {

void CreateOutputFolder(const std::string& path) {
  // create output folder.
  auto out_dir_path = std::filesystem::path(path).parent_path();
  if (out_dir_path.empty()) {
    return;  // create file under CWD, no need to create parent dir
  }

  std::error_code ec;
  std::filesystem::create_directory(out_dir_path, ec);
  YACL_ENFORCE(ec.value() == 0,
               "failed to create output dir={} for path={}, reason = {}",
               out_dir_path.string(), path, ec.message());
}

}  // namespace psi::psi
