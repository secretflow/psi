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

// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "psi/apsi_wrapper/api/wrapper_util.h"

#include <malloc.h>

#include "yacl/base/exception.h"

namespace psi::apsi_wrapper::api {

CString MakeCString(const std::string &str) {
  CString cstr;
  cstr.data = (char *)malloc(str.size() + 1);
  cstr.size = str.size();
  YACL_ENFORCE(cstr.data != nullptr, "malloc str failed, str: {}, size: {}",
               str, str.size());
  char *buf = cstr.data;
  for (auto c : str) {
    *buf = c;
    buf++;
  }
  *buf = '\0';
  return cstr;
}
std::string MakeString(CString str) { return std::string(str.data, str.size); }

CStringArray MakeCStringArray(const std::vector<std::string> &strs) {
  CStringArray cstrs;
  cstrs.data = (CString *)malloc(sizeof(CString) * strs.size());
  YACL_ENFORCE(cstrs.data != nullptr, "malloc strs failed, size: {}",
               strs.size());
  for (size_t i = 0; i < strs.size(); ++i) {
    cstrs.data[i] = MakeCString(strs[i]);
  }
  cstrs.size = strs.size();
  return cstrs;
}

std::vector<std::string> MakeStringArray(CStringArray strs) {
  std::vector<std::string> ret;
  for (size_t i = 0; i != strs.size; ++i) {
    ret.emplace_back(strs.data[i].data, strs.data[i].size);
  }
  return ret;
}

}  // namespace psi::apsi_wrapper::api