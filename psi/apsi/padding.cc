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

#include "psi/apsi/padding.h"

#include "yacl/base/exception.h"

namespace psi::apsi {

// pad data to max_len bytes
// format len(32bit)||data||00..00
std::vector<uint8_t> PaddingData(yacl::ByteContainerView data, size_t max_len) {
  YACL_ENFORCE((data.size() + 4) <= max_len, "data_size:{} max_len:{}",
               data.size(), max_len);

  std::vector<uint8_t> data_with_padding(max_len);
  uint32_t data_size = data.size();

  std::memcpy(&data_with_padding[0], &data_size, sizeof(uint32_t));
  std::memcpy(&data_with_padding[4], data.data(), data.size());

  return data_with_padding;
}

std::string UnPaddingData(yacl::ByteContainerView data) {
  uint32_t data_len;
  std::memcpy(&data_len, data.data(), sizeof(data_len));

  YACL_ENFORCE((data_len + sizeof(uint32_t)) <= data.size());

  std::string ret(data_len, '\0');
  std::memcpy(ret.data(), data.data() + 4, data_len);

  return ret;
}

}  // namespace psi::apsi
