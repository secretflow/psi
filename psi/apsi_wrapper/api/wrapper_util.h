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

#pragma once

#include <string>
#include <vector>

#include "psi/apsi_wrapper/api/wrapper_common.h"

namespace psi::apsi_wrapper::api {

CString MakeCString(const std::string &str);

std::string MakeString(CString);

CStringArray MakeCStringArray(const std::vector<std::string> &strs);

std::vector<std::string> MakeStringArray(CStringArray);

}  // namespace psi::apsi_wrapper::api