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

extern "C" {

#include <stddef.h>
#include <stdlib.h>

struct CString {
  char *data;
  size_t size;
};

struct CStringArray {
  CString *data;
  size_t size;
};

inline void FreeCString(CString *cstr) {
  free(cstr->data);
  cstr->size = 0;
}

inline void FreeCStringArray(CStringArray *array) {
  for (size_t i = 0; i < array->size; ++i) {
    free(array->data[i].data);
  }
  free(array->data);
  array->size = 0;
}
}