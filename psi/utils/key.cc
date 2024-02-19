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

#include "psi/utils/key.h"

#include <omp.h>

#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/utils/csv_header_parser.h"
#include "psi/utils/io.h"

namespace psi {

// Multiple-Key out-of-core sort.
// Out-of-core support reference:
//   http://vkundeti.blogspot.com/2008/03/tech-algorithmic-details-of-unix-sort.html
// Multiple-Key support reference:
//   https://stackoverflow.com/questions/9471101/sort-csv-file-by-column-priority-using-the-sort-command
// use POSIX locale for sort
//   https://unix.stackexchange.com/questions/43465/whats-the-default-order-of-linux-sort/43466
//
// NOTE:
// This implementation requires `sort` command, which is guaranteed by our
// docker-way ship.
void MultiKeySort(const std::string& in_csv, const std::string& out_csv,
                  const std::vector<std::string>& keys, bool numeric_sort,
                  bool unique) {
  CsvHeaderParser parser(in_csv);

  std::string line;
  {
    // Copy head line to out_csv
    // Add scope to flush write here.
    io::FileIoOptions in_opts(in_csv);
    auto in = io::BuildInputStream(in_opts);
    in->GetLine(&line);
    in->Close();

    io::FileIoOptions out_opts(out_csv);
    auto out = io::BuildOutputStream(out_opts);
    out->Write(line);
    out->Write("\n");
    out->Close();
  }

  // Construct sort key indices.
  // NOTE: `sort` cmd starts from index 1.
  std::vector<std::string> sort_keys;
  for (size_t index : parser.target_indices(keys, 1)) {
    // About `sort --key=KEYDEF`
    //
    // KEYDEF is F[.C][OPTS][,F[.C][OPTS]] for start and stop position, where
    // F is a field number and C a character position in the field; both are
    // origin 1, and the stop position defaults to the line's end.  If neither
    // -t nor -b is in effect, characters in a field are counted from the
    // beginning of the preceding whitespace.  OPTS is one or more
    // single-letter ordering options [bdfgiMhnRrV], which override global
    // ordering options for that key.  If no key is given, use the entire line
    // as the key.
    //
    // I have already verified `sort --key=3,3 --key=1,1` will firstly sort by
    // 3rd field and then 1st field.
    sort_keys.push_back(fmt::format("--key={},{}", index, index));
  }
  YACL_ENFORCE(sort_keys.size() == keys.size(),
               "mismatched header, field_names={}, line={}",
               fmt::join(keys, ","), line);

  int mcpus = omp_get_num_procs();

  // Sort the csv body and append to out csv.
  std::string cmd = fmt::format(
      "tail -n +2 {} | LC_ALL=C sort {} --parallel={} --buffer-size=1G "
      "--stable "
      "--field-separator=, {} {} >>{}",
      in_csv, numeric_sort ? "-n" : "", mcpus, fmt::join(sort_keys, " "),
      unique ? "| LC_ALL=C uniq" : "", out_csv);
  SPDLOG_INFO("Executing sort scripts: {}", cmd);
  int ret = system(cmd.c_str());
  SPDLOG_INFO("Finished sort scripts: {}, ret={}", cmd, ret);
  YACL_ENFORCE(ret == 0, "failed to execute cmd={}, ret={}", cmd, ret);
}

std::string KeysJoin(const std::vector<absl::string_view>& keys) {
  return absl::StrJoin(keys, ",");
}

}  // namespace psi
