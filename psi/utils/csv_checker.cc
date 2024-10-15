// Copyright 2022 Ant Group Co., Ltd.
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

#include "psi/utils/csv_checker.h"

#include <fmt/core.h>
#include <omp.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_set>

#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"
#include "arrow/api.h"
#include "arrow/csv/api.h"
#include "arrow/io/api.h"
#include "arrow/ipc/api.h"
#include "arrow/pretty_print.h"
#include "boost/uuid/uuid.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "yacl/crypto/hash/hash_utils.h"
#include "yacl/utils/scope_guard.h"

#include "psi/utils/io.h"
#include "psi/utils/key.h"

namespace psi {
namespace {
// Check if the first line starts with BOM(Byte Order Mark).
bool CheckIfBOMExists(const std::string& file_path) {
  std::string first_line;
  {
    io::FileIoOptions file_opts(file_path);
    auto file_is = io::BuildInputStream(file_opts);
    file_is->GetLine(&first_line, '\n');
    file_is->Close();
  }

  // Only detect UTF-8 BOM right now.
  return first_line.length() >= 3 && first_line[0] == '\xEF' &&
         first_line[1] == '\xBB' && first_line[2] == '\xBF';
}
}  // namespace

CsvChecker::CsvChecker(const std::string& csv_path,
                       const std::vector<std::string>& schema_names,
                       bool skip_check) {
  YACL_ENFORCE(!CheckIfBOMExists(csv_path),
               "the file {} starts with BOM(Byte Order Mark).", csv_path);

  size_t duplicated_size = 0;
  std::vector<std::string> duplicated_keys;
  yacl::crypto::SslHash hash_obj(yacl::crypto::HashAlgorithm::SHA256);

  io::FileIoOptions file_opts(csv_path);
  io::CsvOptions csv_opts;
  csv_opts.read_options.file_schema.feature_names = schema_names;
  csv_opts.read_options.file_schema.feature_types.resize(schema_names.size(),
                                                         io::Schema::STRING);
  auto csv_reader = io::BuildReader(file_opts, csv_opts);

  boost::uuids::random_generator uuid_generator;
  auto uuid_str = boost::uuids::to_string(uuid_generator());
  std::string keys_file = fmt::format("selected-keys.{}", uuid_str);

  io::FileIoOptions tmp_file_ops(keys_file);
  auto keys_os = io::BuildOutputStream(tmp_file_ops);
  ON_SCOPE_EXIT([&] {
    std::error_code ec;
    std::filesystem::remove(tmp_file_ops.file_name, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove tmp file: {}, msg: {}",
                  tmp_file_ops.file_name, ec.message());
    }
  });

  // read csv file by row
  io::ColumnVectorBatch batch;
  while (csv_reader->Next(&batch)) {
    for (size_t row = 0; row < batch.Shape().rows; row++) {
      std::vector<absl::string_view> chosen;
      for (size_t col = 0; col < batch.Shape().cols; col++) {
        const auto& token = batch.At<std::string>(row, col);
        YACL_ENFORCE(token.size(), "empty token in row={} field={}",
                     data_count_ + row, schema_names[col]);
        chosen.push_back(token);
      }
      // if combined_id is about 128bytes
      // .keys file tasks almost 12GB for 10^8 samples.
      std::string combined_id = KeysJoin(chosen);
      hash_obj.Update(combined_id);
      if (!skip_check) {
        keys_os->Write(combined_id.data(), combined_id.size());
        keys_os->Write("\n", 1);
      }
    }
    data_count_ += batch.Shape().rows;
  }
  keys_os->Close();

  if (!skip_check) {
    std::string duplicated_keys_file =
        fmt::format("duplicate-keys.{}", uuid_str);
    ON_SCOPE_EXIT([&] {
      std::error_code ec;
      std::filesystem::remove(duplicated_keys_file, ec);
      if (ec.value() != 0) {
        SPDLOG_WARN("can not remove tmp file: {}, msg: {}",
                    duplicated_keys_file, ec.message());
      }
    });

    int mcpus = omp_get_num_procs();

    std::string cmd = fmt::format(
        "LC_ALL=C sort --parallel={} --buffer-size=1G --stable {} | LC_ALL=C "
        "uniq -d > {}",
        mcpus, keys_file, duplicated_keys_file);
    SPDLOG_INFO("Executing duplicated scripts: {}", cmd);
    int ret = system(cmd.c_str());
    YACL_ENFORCE(ret == 0, "failed to execute cmd={}, ret={}", cmd, ret);
    io::FileIoOptions dup_keys_file_opts(duplicated_keys_file);
    auto duplicated_is = io::BuildInputStream(dup_keys_file_opts);
    std::string duplicated_key;
    while (duplicated_is->GetLine(&duplicated_key)) {
      if (duplicated_size++ < 10) {
        duplicated_keys.push_back(duplicated_key);
      }
    }
    // not precise size if some key repeat more than 2 times.
    YACL_ENFORCE(duplicated_size == 0, "found duplicated keys: {}",
                 fmt::join(duplicated_keys, ","));
  }

  std::vector<uint8_t> digest = hash_obj.CumulativeHash();
  hash_digest_ = absl::BytesToHexString(absl::string_view(
      reinterpret_cast<const char*>(digest.data()), digest.size()));
}

CheckCsvReport CheckCsv(const std::string& input_file_path,
                        const std::vector<std::string>& keys,
                        bool check_duplicates, bool generate_key_hash_digest) {
  CheckCsvReport report;

  boost::uuids::random_generator uuid_generator;
  auto uuid_str = boost::uuids::to_string(uuid_generator());

  std::string output_file_path = std::filesystem::temp_directory_path() /
                                 fmt::format("{}.psi_checked", uuid_str);

  YACL_ENFORCE(std::filesystem::exists(input_file_path),
               "Input file {} doesn't exist.", input_file_path);

  // Read input file and generate output file by filtering unselected keys.
  arrow::io::IOContext io_context = arrow::io::default_io_context();
  std::shared_ptr<arrow::io::ReadableFile> infile;

  infile = arrow::io::ReadableFile::Open(input_file_path,
                                         arrow::default_memory_pool())
               .ValueOrDie();
  auto read_options = arrow::csv::ReadOptions::Defaults();
  auto parse_options = arrow::csv::ParseOptions::Defaults();
  auto convert_options = arrow::csv::ConvertOptions::Defaults();
  convert_options.include_columns = keys;

  auto reader =
      arrow::csv::StreamingReader::Make(io_context, infile, read_options,
                                        parse_options, convert_options)
          .ValueOrDie();

  const std::shared_ptr<arrow::Schema>& schema = reader->schema();

  std::shared_ptr<arrow::io::FileOutputStream> outfile =
      arrow::io::FileOutputStream::Open(output_file_path).ValueOrDie();

  auto write_options = arrow::csv::WriteOptions::Defaults();
  write_options.include_header = true;

  auto writer =
      arrow::csv::MakeCSVWriter(outfile, schema, write_options).ValueOrDie();

  std::shared_ptr<arrow::RecordBatch> batch;

  while (true) {
    arrow::Status status = reader->ReadNext(&batch);

    if (!status.ok()) {
      YACL_THROW("Read csv error.");
    }

    if (batch == NULL) {
      // Handle end of file
      break;
    }

    report.num_rows += batch->num_rows();

    if (check_duplicates || generate_key_hash_digest) {
      if (!writer->WriteRecordBatch(*batch).ok()) {
        YACL_THROW("writer WriteRecordBatch failed.");
      }
    }
  }

  if (!writer->Close().ok()) {
    YACL_THROW("writer Close failed.");
  }
  if (!outfile->Close().ok()) {
    YACL_THROW("outfile Close failed.");
  }
  if (!infile->Close().ok()) {
    YACL_THROW("infile Close failed.");
  }

  // Check duplicates.
  if (check_duplicates) {
    std::string duplicated_output_file_path =
        std::filesystem::temp_directory_path() /
        fmt::format("{}.psi_checked_duplicates", uuid_str);

    int mcpus = omp_get_num_procs();
    std::string cmd = fmt::format(
        "LC_ALL=C tail -n +2 {} | "  // skip header
        "LC_ALL=C sort --parallel={} --buffer-size=1G "
        "--stable | LC_ALL=C uniq -d > {}",
        output_file_path, mcpus, duplicated_output_file_path);
    SPDLOG_INFO("Executing script to get duplicates: {}", cmd);
    int ret = system(cmd.c_str());
    YACL_ENFORCE(ret == 0, "Failed to execute cmd={}, ret={}", cmd, ret);

    infile = arrow::io::ReadableFile::Open(duplicated_output_file_path,
                                           arrow::default_memory_pool())
                 .ValueOrDie();
    size_t file_size = infile->GetSize().ValueOrDie();
    if (file_size != 0) {
      SPDLOG_WARN("Input file contains duplicate: {}", input_file_path);
      report.contains_duplicates = true;
      report.duplicates_keys_file_path = duplicated_output_file_path;
    } else {
      report.contains_duplicates = false;
    }

    if (!infile->Close().ok()) {
      YACL_THROW("infile Close failed.");
    }
  }

  // Get hash digest.
  if (generate_key_hash_digest) {
#ifdef __APPLE__
    std::string command(fmt::format("shasum -a 256 {}", output_file_path));
#else
    std::string command(fmt::format("sha256sum {}", output_file_path));
#endif
    std::array<char, 128> buffer;
    std::string result;
    SPDLOG_INFO("Executing script to get hash digest: {}", command);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
      YACL_ENFORCE("Couldn't execute cmd: {}", command);
    }
    while (fgets(buffer.data(), 128, pipe) != NULL) {
      result += buffer.data();
    }
    int return_code = pclose(pipe);

    if (return_code != 0) {
      YACL_ENFORCE("Execute cmd {} failed, return code is {}", command,
                   return_code);
    }

    std::string hash_digest = result.substr(0, 64);

    report.key_hash_digest = hash_digest;
  }

  return report;
}

}  // namespace psi
