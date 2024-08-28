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

#include "apsi/oprf/oprf_sender.h"
#include "apsi/psi_params.h"
#include "apsi/sender_db.h"

#include "psi/apsi_wrapper/utils/csv_reader.h"

namespace psi::apsi_wrapper {

std::shared_ptr<::apsi::sender::SenderDB> TryLoadSenderDB(
    std::istream &in_stream, ::apsi::oprf::OPRFKey &oprf_key);

std::shared_ptr<::apsi::sender::SenderDB> TryLoadSenderDB(
    const std::string &db_file, const std::string &params_file,
    ::apsi::oprf::OPRFKey &oprf_key);

std::shared_ptr<::apsi::sender::SenderDB> GenerateSenderDB(
    const std::string &db_file, const std::string &params_file,
    size_t nonce_byte_count, bool compress, ::apsi::oprf::OPRFKey &oprf_key,
    const std::vector<std::string> &keys = {},
    const std::vector<std::string> &labels = {});

std::unique_ptr<psi::apsi_wrapper::DBData> load_db(
    const std::string &db_file, const std::vector<std::string> &keys = {},
    const std::vector<std::string> &labels = {});

std::pair<std::unique_ptr<psi::apsi_wrapper::DBData>, std::vector<std::string>>
load_db_with_orig_items(const std::string &db_file);

bool TrySaveSenderDB(const std::string &sdb_out_file,
                     std::shared_ptr<::apsi::sender::SenderDB> sender_db,
                     const ::apsi::oprf::OPRFKey &oprf_key);

bool TrySaveSenderDB(std::ostream &os,
                     std::shared_ptr<::apsi::sender::SenderDB> sender_db,
                     const ::apsi::oprf::OPRFKey &oprf_key);

std::shared_ptr<::apsi::sender::SenderDB> create_sender_db(
    const psi::apsi_wrapper::DBData &db_data,
    std::unique_ptr<::apsi::PSIParams> psi_params,
    ::apsi::oprf::OPRFKey &oprf_key, size_t nonce_byte_count, bool compress);

}  // namespace psi::apsi_wrapper
