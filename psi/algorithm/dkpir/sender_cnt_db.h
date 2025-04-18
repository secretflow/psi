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

#pragma once

#include "apsi/sender_db.h"

#include "psi/wrapper/apsi/utils/common.h"

#include "psi/proto/psi.pb.h"

namespace psi::dkpir {
std::shared_ptr<::apsi::sender::SenderDB> GenerateSenderCntDB(
    const std::string &db_file, const std::string &params_file,
    const std::string &sk_file, size_t nonce_byte_count, bool compress,
    CurveType curve_type, ::apsi::oprf::OPRFKey &oprf_key,
    const std::vector<std::string> &keys = {},
    const std::vector<std::string> &labels = {});

std::shared_ptr<::apsi::sender::SenderDB> CreateSenderCntDB(
    const psi::apsi_wrapper::DBData &db_data,
    std::unique_ptr<::apsi::PSIParams> psi_params, const std::string &sk_file,
    CurveType curve_type, ::apsi::oprf::OPRFKey &oprf_key,
    size_t nonce_byte_count, bool compress);

}  // namespace psi::dkpir