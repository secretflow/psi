// Copyright 2025 The secretflow authors.
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

#include "psi/algorithm/ypir/server.h"
#include "experiment/pir/simplepir/util.h"

#include "yacl/crypto/tools/prg.h"

using namespace std;

namespace pir::ypir {
YPIRServer::YPIRServer(size_t row_num, size_t col_num, size_t d1,
    size_t d2, uint64_t p, uint64_t q1, uint64_t q2, uint128_t seed) : 
    row_num_(row_num), col_num_(col_num), d1_(d1),
    d2_(d2), p_(p), q1_(q1), q2_(q2), seed_(seed) {
  YACL_ENFORCE(row_num > 0, "row_num must be positive");
  YACL_ENFORCE(col_num > 0, "col_num must be positive");
  YACL_ENFORCE(d1 > 0, "d1 must be positive");
  YACL_ENFORCE(d2 > 0, "d2 must be positive");
  YACL_ENFORCE(p > 0, "p must be positive");
  YACL_ENFORCE(q1 > 0, "q1 must be positive");
  YACL_ENFORCE(q2 > 0, "q2 must be positive");
  YACL_ENFORCE(row_num % d1 == 0, "row_num must be divisible by d1");
  YACL_ENFORCE(col_num % d2 == 0, "col_num must be divisible by d2");

  delta1_ = q1_ / p_;
  delta2_ = q2_ / p_;
}

void YPIRServer::gen_db() {
  database_.resize(row_num_);
  for (size_t i = 0; i < row_num_; i++) {
      database_[i] = pir::simple::GenerateRandomVector(col_num_, p_, true);
  }
}

}