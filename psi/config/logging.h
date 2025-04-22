// Copyright 2025 Ant Group Co., Ltd.
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

#include <string>

namespace psi::api {

// Logging level for default logger.
// Default to info.
// Supports:
//
// - trace: SPDLOG_LEVEL_TRACE
// - debug: SPDLOG_LEVEL_DEBUG
// - info: SPDLOG_LEVEL_INFO
// - warn: SPDLOG_LEVEL_WARN
// - err: SPDLOG_LEVEL_ERROR
// - critical: SPDLOG_LEVEL_CRITICAL
// - off: SPDLOG_LEVEL_OFF
//
struct LoggingOptions {
  std::string logging_level = "SPDLOG_LEVEL_INFO";

  // The path of trace.
  // Deafult to /tmp/psi.trace
  std::string trace_path = "/tmp/psi.trace";
};

}  // namespace psi::api
