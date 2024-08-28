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

#include <variant>

#include "apsi/match_record.h"
#include "apsi/psi_params.h"
#include "arrow/csv/api.h"
#include "arrow/datum.h"
#include "arrow/io/api.h"

namespace psi::apsi_wrapper {

using UnlabeledData = std::vector<::apsi::Item>;

using LabeledData = std::vector<std::pair<::apsi::Item, ::apsi::Label>>;

using DBData = std::variant<UnlabeledData, LabeledData>;

bool IsDuplicated(const DBData &db_data);

/**
Throw an exception if the given file is invalid.
*/
void throw_if_file_invalid(const std::string &file_name);

void throw_if_directory_invalid(const std::string &dir_name);

std::unique_ptr<::apsi::PSIParams> BuildPsiParams(
    const std::string &params_file);

int print_intersection_results(
    const std::vector<std::string> &orig_items,
    const std::vector<::apsi::Item> &items,
    const std::vector<::apsi::receiver::MatchRecord> &intersection,
    const std::string &out_file, bool append_to_outfile = false);

std::shared_ptr<arrow::csv::StreamingReader> MakeArrowCsvReader(
    const std::string &file_name, std::vector<std::string> column_names);

std::shared_ptr<arrow::csv::StreamingReader> MakeArrowCsvReader(
    const std::string &file_name,
    std::unordered_map<std::string, std::shared_ptr<arrow::DataType>>
        column_types);

}  // namespace psi::apsi_wrapper
