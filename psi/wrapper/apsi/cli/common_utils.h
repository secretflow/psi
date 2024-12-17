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

// STD
#include <string>
#include <vector>

// APSI
#include "apsi/util/stopwatch.h"

namespace psi::apsi_wrapper::cli {

/**
Prepare console for color output.
*/
void prepare_console();

/**
Generate timing report for timespans.
*/
std::vector<std::string> generate_timespan_report(
    const std::vector<apsi::util::Stopwatch::TimespanSummary> &timespans,
    int max_name_length);

/**
Generate timing report for single events.
*/
std::vector<std::string> generate_event_report(
    const std::vector<apsi::util::Stopwatch::Timepoint> &timepoints,
    int max_name_length);

/**
Print timings.
*/
void print_timing_report(const apsi::util::Stopwatch &stopwatch);

}  // namespace psi::apsi_wrapper::cli
