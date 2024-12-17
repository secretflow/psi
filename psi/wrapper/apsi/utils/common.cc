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

#include "psi/wrapper/apsi/utils/common.h"

#include <ios>
#include <utility>

#if defined(_MSC_VER)
#include <windows.h>
#endif
#if defined(__GNUC__) && (__GNUC__ < 8) && !defined(__clang__)
#include <experimental/filesystem>
#else
#include <filesystem>
#endif
#include <fstream>
#include <iostream>

namespace psi::apsi_wrapper {

using namespace std;
#if defined(__GNUC__) && (__GNUC__ < 8) && !defined(__clang__)
namespace fs = std::experimental::filesystem;
#else
namespace fs = std::filesystem;
#endif

struct Colors {
  static const string Red;
  static const string Green;
  static const string RedBold;
  static const string GreenBold;
  static const string Reset;
};

const string Colors::Red = "\033[31m";
const string Colors::Green = "\033[32m";
const string Colors::RedBold = "\033[1;31m";
const string Colors::GreenBold = "\033[1;32m";
const string Colors::Reset = "\033[0m";

std::shared_ptr<arrow::csv::StreamingReader> MakeArrowCsvReader(
    const std::string &file_name, std::vector<std::string> column_names) {
  std::unordered_map<std::string, std::shared_ptr<arrow::DataType>>
      column_types;
  for (auto &col : column_names) {
    column_types[col] = arrow::utf8();
  }
  return MakeArrowCsvReader(file_name, std::move(column_types));
}

bool IsDuplicated(const LabeledData &labeled_data) {
  std::unordered_set<std::string> labeled_data_set;
  for (auto &label : labeled_data) {
    labeled_data_set.insert(label.first.to_string());
  }

  return labeled_data.size() != labeled_data_set.size();
}

bool IsDuplicated(const UnlabeledData &unlabeled_data) {
  std::unordered_set<std::string> data_set;
  for (auto &data : unlabeled_data) {
    data_set.insert(data.to_string());
  }

  return data_set.size() != unlabeled_data.size();
}

bool IsDuplicated(const DBData &db_data) {
  if (std::holds_alternative<LabeledData>(db_data)) {
    return IsDuplicated(std::get<LabeledData>(db_data));
  } else {
    return IsDuplicated(std::get<UnlabeledData>(db_data));
  }
}

std::shared_ptr<arrow::csv::StreamingReader> MakeArrowCsvReader(
    const std::string &file_name,
    std::unordered_map<std::string, std::shared_ptr<arrow::DataType>>
        column_types) {
  auto infile =
      arrow::io::ReadableFile::Open(file_name, arrow::default_memory_pool())
          .ValueOrDie();

  arrow::io::IOContext io_context = arrow::io::default_io_context();
  auto read_options = arrow::csv::ReadOptions::Defaults();

  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.column_types = std::move(column_types);

  return arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                           parse_options, convert_options)
      .ValueOrDie();
}

void throw_if_file_invalid(const string &file_name) {
  fs::path file(file_name);

  if (!fs::exists(file)) {
    APSI_LOG_ERROR("File `" << file.string() << "` does not exist");
    throw logic_error("file does not exist");
  }
  if (!fs::is_regular_file(file)) {
    APSI_LOG_ERROR("File `" << file.string() << "` is not a regular file");
    throw logic_error("invalid file");
  }
}

void throw_if_directory_invalid(const std::string &dir_name) {
  fs::path dir(dir_name);

  if (!fs::exists(dir)) {
    APSI_LOG_ERROR("Directory `" << dir.string() << "` does not exist");
    throw logic_error("directory does not exist");
  }
  if (!fs::is_directory(dir)) {
    APSI_LOG_ERROR("Directory `" << dir.string() << "` is not a regular file");
    throw logic_error("invalid file");
  }
}

std::unique_ptr<::apsi::PSIParams> BuildPsiParams(
    const std::string &params_file) {
  string params_json;

  try {
    throw_if_file_invalid(params_file);
    std::fstream input_file(params_file, ios_base::in);

    if (!input_file.is_open()) {
      APSI_LOG_ERROR("File " << params_file
                             << " could not be open for reading.");
      throw runtime_error("Could not open params file");
    }

    string line;
    while (getline(input_file, line)) {
      params_json.append(line);
      params_json.append("\n");
    }

    input_file.close();
  } catch (const exception &ex) {
    APSI_LOG_ERROR("Error trying to read input file " << params_file << ": "
                                                      << ex.what());
    return nullptr;
  }

  unique_ptr<::apsi::PSIParams> params;
  try {
    params =
        make_unique<::apsi::PSIParams>(::apsi::PSIParams::Load(params_json));
  } catch (const exception &ex) {
    APSI_LOG_ERROR(
        "APSI threw an exception creating ::apsi::PSIParams: " << ex.what());
    return nullptr;
  }

  APSI_LOG_INFO("::apsi::PSIParams have false-positive probability 2^("
                << params->log2_fpp() << ") per receiver item");

  return params;
}

int stream_intersection_results(
    const vector<std::string> &orig_items, const vector<::apsi::Item> &items,
    const vector<::apsi::receiver::MatchRecord> &intersection,
    std::vector<std::string> &keys, std::vector<std::string> &values) {
  if (orig_items.size() != items.size()) {
    throw invalid_argument("orig_items must have same size as items");
  }

  int match_cnt = 0;
  for (size_t i = 0; i < orig_items.size(); i++) {
    std::stringstream msg;
    if (intersection[i].found) {
      match_cnt++;
      msg << Colors::GreenBold << orig_items[i] << Colors::Reset << "(FOUND) ";
      keys.push_back(orig_items[i]);
      if (intersection[i].label) {
        msg << ": ";
        msg << Colors::GreenBold << intersection[i].label.to_string()
            << Colors::Reset;
        values.push_back(intersection[i].label.to_string());
      }
      APSI_LOG_INFO(msg.str());
    } else {
      // msg << Colors::RedBold << orig_items[i] << Colors::Reset << " (NOT
      // FOUND)"; APSI_LOG_INFO(msg.str());
    }
  }

  return match_cnt;
}

int print_intersection_results(
    const vector<std::string> &orig_items, const vector<::apsi::Item> &items,
    const vector<::apsi::receiver::MatchRecord> &intersection,
    const string &out_file, bool append_to_outfile) {
  if (orig_items.size() != items.size()) {
    throw invalid_argument("orig_items must have same size as items");
  }

  std::stringstream csv_output;
  std::string csv_header = "key";
  int match_cnt = 0;
  for (size_t i = 0; i < orig_items.size(); i++) {
    std::stringstream msg;
    if (intersection[i].found) {
      match_cnt++;
      msg << Colors::GreenBold << orig_items[i] << Colors::Reset << "(FOUND) ";
      csv_output << orig_items[i];
      if (intersection[i].label) {
        msg << ": ";
        msg << Colors::GreenBold << intersection[i].label.to_string()
            << Colors::Reset;
        csv_output << "," << intersection[i].label.to_string();
        csv_header += ",value";
      }
      csv_output << endl;
      APSI_LOG_INFO(msg.str());
    } else {
      // msg << Colors::RedBold << orig_items[i] << Colors::Reset << " (NOT
      // FOUND)"; APSI_LOG_INFO(msg.str());
    }
  }

  if (!out_file.empty()) {
    if (append_to_outfile) {
      std::ofstream ofs(out_file, std::ios_base::app);
      ofs << csv_output.str();
    } else {
      std::ofstream ofs(out_file);
      ofs << csv_header << endl;
      ofs << csv_output.str();
    }

    APSI_LOG_INFO("Wrote output to " << out_file);
  }

  return match_cnt;
}

}  // namespace psi::apsi_wrapper
