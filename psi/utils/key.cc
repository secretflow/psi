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

#include <fstream>
#include <sstream>

#include "fmt/ranges.h"
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"

#include "psi/utils/arrow_helper.h"
#include "psi/utils/io.h"

namespace psi {

namespace {

// parse cpuset.cpus format(eg. "0-3,5")
int ParseCpuset(const std::string& cpuset) {
  int count = 0;
  std::stringstream ss(cpuset);
  std::string item;
  while (std::getline(ss, item, ',')) {
    size_t dash = item.find('-');
    if (dash != std::string::npos) {
      int start = std::stoi(item.substr(0, dash));
      int end = std::stoi(item.substr(dash + 1));
      count += end - start + 1;
    } else {
      if (!item.empty()) {
        count++;
      }
    }
  }
  return count;
}

int GetCpuCount() {
  // 1. check cpuset
  std::ifstream cpuset_file("/sys/fs/cgroup/cpuset/cpuset.cpus");
  if (cpuset_file) {
    std::string cpuset;
    if (std::getline(cpuset_file, cpuset)) {
      if (!cpuset.empty() && cpuset != "0") {
        return ParseCpuset(cpuset);
      }
    }
  }

  // 2. check CPU quota and period
  // 2.1 cgroup v1
  std::ifstream quota_file("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us");
  std::ifstream period_file("/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us");
  if (quota_file && period_file) {
    int64_t quota;
    int64_t period;
    quota_file >> quota;
    period_file >> period;
    if (quota > 0 && period > 0) {
      return static_cast<int>((quota + period - 1) / period);
    }
  }

  // 2.2 cgroup v2
  std::ifstream v2_quota_file("/sys/fs/cgroup/cpu.max");
  if (v2_quota_file) {
    std::string line;
    if (std::getline(v2_quota_file, line)) {
      std::istringstream iss(line);
      std::string quota_str;
      std::string period_str;
      if (iss >> quota_str >> period_str) {
        if (quota_str != "max") {
          try {
            int64_t quota = std::stoll(quota_str);
            int64_t period = std::stoll(period_str);
            if (period > 0) {
              return static_cast<int>((quota + period - 1) / period);
            }
          } catch (...) {
            SPDLOG_WARN(
                "trans quota({}) or period({}) failed, use default method",
                quota_str, period_str);
          }
        }
      }
    }
  }

  // 3. use hardware_concurrency
  return std::thread::hardware_concurrency();
}

}  // namespace

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
  auto csv_reader = MakeCsvReader(in_csv);
  auto schema = csv_reader->schema();

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
  std::vector<std::string> sort_keys;
  for (auto key : keys) {
    auto index = schema->GetFieldIndex(key);
    YACL_ENFORCE(index >= 0, "field {} is not found in {}", key, in_csv);
    // NOTE: `sort` cmd starts from index 1.
    auto sort_index = index + 1;
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
    sort_keys.push_back(fmt::format("--key={},{}", sort_index, sort_index));
  }
  YACL_ENFORCE(sort_keys.size() == keys.size(),
               "mismatched header, field_names={}, line={}",
               fmt::join(keys, ","), line);

  int mcpus = GetCpuCount();

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

std::string KeysJoin(const std::vector<absl::string_view>& keys, char sep) {
  return absl::StrJoin(keys, std::string(&sep, 1));
}

}  // namespace psi
