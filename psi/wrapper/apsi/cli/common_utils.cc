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

#include "psi/wrapper/apsi/cli/common_utils.h"

// STD
#include <iomanip>
#include <iostream>

// APSI
#include "apsi/log.h"

namespace psi::apsi_wrapper::cli {

using namespace std;
using namespace apsi;
using namespace apsi::util;

/**
This only turns on showing colors for Windows.
*/
void prepare_console() {
#ifndef _MSC_VER
  return;  // Nothing to do on Linux.
#else
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole == INVALID_HANDLE_VALUE) return;

  DWORD dwMode = 0;
  if (!GetConsoleMode(hConsole, &dwMode)) return;

  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hConsole, dwMode);
#endif
}

vector<string> generate_timespan_report(
    const vector<Stopwatch::TimespanSummary> &timespans, int max_name_length) {
  vector<string> report;

  for (const auto &timespan : timespans) {
    stringstream ss;
    ss << setw(max_name_length) << left << timespan.event_name << ": "
       << setw(5) << right << timespan.event_count << " instances. ";
    if (timespan.event_count == 1) {
      ss << "Duration: " << setw(6) << right << static_cast<int>(timespan.avg)
         << "ms";
    } else {
      ss << "Average:  " << setw(6) << right << static_cast<int>(timespan.avg)
         << "ms Minimum: " << setw(6) << right << timespan.min
         << "ms Maximum: " << setw(6) << right << timespan.max << "ms";
    }

    report.push_back(ss.str());
  }

  return report;
}

vector<string> generate_event_report(
    const vector<Stopwatch::Timepoint> &timepoints, int max_name_length) {
  vector<string> report;

  Stopwatch::time_unit last = Stopwatch::start_time;
  for (const auto &timepoint : timepoints) {
    stringstream ss;

    int64_t since_start = chrono::duration_cast<chrono::milliseconds>(
                              timepoint.time_point - Stopwatch::start_time)
                              .count();
    int64_t since_last =
        chrono::duration_cast<chrono::milliseconds>(timepoint.time_point - last)
            .count();

    ss << setw(max_name_length) << left << timepoint.event_name << ": "
       << setw(6) << right << since_start << "ms since start, " << setw(6)
       << right << since_last << "ms since last single event.";
    last = timepoint.time_point;
    report.push_back(ss.str());
  }

  return report;
}

void print_timing_report(const Stopwatch &stopwatch) {
  vector<string> timing_report;
  vector<Stopwatch::TimespanSummary> timings;
  stopwatch.get_timespans(timings);

  if (timings.size() > 0) {
    timing_report = generate_timespan_report(
        timings, stopwatch.get_max_timespan_event_name_length());

    APSI_LOG_INFO("Timespan event information");
    for (const auto &timing : timing_report) {
      APSI_LOG_INFO(timing.c_str());
    }
  }

  vector<Stopwatch::Timepoint> timepoints;
  stopwatch.get_events(timepoints);

  if (timepoints.size() > 0) {
    timing_report = generate_event_report(
        timepoints, stopwatch.get_max_event_name_length());

    APSI_LOG_INFO("Single event information");
    for (const auto &timing : timing_report) {
      APSI_LOG_INFO(timing.c_str());
    }
  }
}

}  // namespace psi::apsi_wrapper::cli
