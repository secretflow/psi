// Copyright 2024 Ant Group Co., Ltd.
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

#include "psi/utils/ec.h"

#include <filesystem>

#include "psi/cryptor/ecc_cryptor.h"
#include "psi/utils/io.h"

namespace psi {

std::vector<uint8_t> ReadEcSecretKeyFile(const std::string& file_path) {
  size_t file_byte_size = 0;
  try {
    file_byte_size = std::filesystem::file_size(file_path);
  } catch (std::filesystem::filesystem_error& e) {
    YACL_THROW("ReadEcSecretKeyFile {} Error: {}", file_path, e.what());
  }
  YACL_ENFORCE(file_byte_size == kEccKeySize,
               "error format: key file bytes is {}, which is required to be {}",
               file_byte_size, kEccKeySize);

  std::vector<uint8_t> secret_key(kEccKeySize);

  auto in = io::BuildInputStream(io::FileIoOptions(file_path));
  in->Read(secret_key.data(), kEccKeySize);
  in->Close();

  return secret_key;
}

}  // namespace psi
