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

#include "psi/algorithm/dkpir/query.h"
#include "psi/wrapper/apsi/sender.h"
#include "psi/wrapper/apsi/yacl_channel.h"

namespace psi::dkpir::DkPirSender {

::apsi::OPRFResponse GenerateOPRFResponse(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key,
    const uint128_t &shuffle_seed, const uint64_t &shuffle_counter);

// Generate and send a response to an OPRF request, which is a shuffled
// result.
void RunOPRF(
    const ::apsi::OPRFRequest &oprf_request, ::apsi::oprf::OPRFKey key,
    const uint128_t &shuffle_seed, const uint64_t &shuffle_counter,
    ::apsi::network::Channel &chl,
    std::function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun =
        psi::apsi_wrapper::Sender::BasicSend<::apsi::Response::element_type>);

// Generate and send a response to a row count query. Compared to the method
// in the superclass, the main modification is to replace the database of the
// query from sender_db to sender_cnt_db
void RunQuery(
    const psi::dkpir::DkPirQuery &query, psi::apsi_wrapper::YaclChannel &chl,
    bool streaming_result = true,
    std::function<void(::apsi::network::Channel &, ::apsi::Response)> send_fun =
        psi::apsi_wrapper::Sender::BasicSend<::apsi::Response::element_type>,
    std::function<void(::apsi::network::Channel &, ::apsi::ResultPart)>
        send_rp_fun = psi::apsi_wrapper::Sender::BasicSend<
            ::apsi::ResultPart::element_type>);

}  // namespace psi::dkpir::DkPirSender