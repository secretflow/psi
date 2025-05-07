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

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "yacl/base/exception.h"
#include "yacl/link/link.h"
#include "yacl/utils/scope_guard.h"

#include "psi/legacy/memory_psi.h"
#include "psi/utils/csv_checker.h"
#include "psi/utils/hash_bucket_cache.h"
#include "psi/utils/index_store.h"
#include "psi/utils/key.h"
#include "psi/utils/progress.h"
#include "psi/utils/random_str.h"

#include "psi/proto/psi.pb.h"

namespace psi {

void CreateOutputFolder(const std::string& path);

// NOTE(junfeng): `indices` must be sorted
size_t FilterFileByIndices(const std::string& input, const std::string& output,
                           const std::vector<uint64_t>& indices,
                           bool output_difference,
                           size_t header_line_count = 1);

size_t FilterFileByIndices(const std::string& input, const std::string& output,
                           const std::filesystem::path& indices,
                           bool output_difference,
                           size_t header_line_count = 1);

std::unique_ptr<CsvChecker> CheckInput(
    std::shared_ptr<yacl::link::Context> lctx, const std::string& input_path,
    const std::vector<std::string>& selected_fields, bool precheck_required);

template <typename T>
size_t GenerateResult(const std::string& input_path,
                      const std::string& output_path,
                      const std::vector<std::string>& selected_fields,
                      const T& indices, bool sort_output, bool digest_equal,
                      bool output_difference = false) {
  // use tmp file to avoid `shell Injection`
  auto uuid_str = GetRandomString();
  auto tmp_sort_in_file = std::filesystem::path(output_path)
                              .parent_path()
                              .append(fmt::format("tmp-sort-in-{}", uuid_str));
  auto tmp_sort_out_file =
      std::filesystem::path(output_path)
          .parent_path()
          .append(fmt::format("tmp-sort-out-{}", uuid_str));
  // register remove of temp file.
  ON_SCOPE_EXIT([&] {
    std::error_code ec;
    std::filesystem::remove(tmp_sort_out_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove tmp file: {}, msg: {}",
                  tmp_sort_out_file.c_str(), ec.message());
    }
    std::filesystem::remove(tmp_sort_in_file, ec);
    if (ec.value() != 0) {
      SPDLOG_WARN("can not remove tmp file: {}, msg: {}",
                  tmp_sort_in_file.c_str(), ec.message());
    }
  });

  size_t cnt = FilterFileByIndices(input_path, tmp_sort_in_file, indices,
                                   output_difference);
  if (sort_output && !digest_equal) {
    MultiKeySort(tmp_sort_in_file, tmp_sort_out_file, selected_fields);
    std::filesystem::rename(tmp_sort_out_file, output_path);
  } else {
    std::filesystem::rename(tmp_sort_in_file, output_path);
  }

  return cnt;
}

// the item order of `item_data_list` and `item_list` needs to be the same
void GetResultIndices(const std::vector<std::string>& item_data_list,
                      const std::vector<HashBucketCache::BucketItem>& item_list,
                      std::vector<std::string>& result_list,
                      std::vector<uint64_t>* indices);

class BucketPsi {
 public:
  explicit BucketPsi(BucketPsiConfig config,
                     std::shared_ptr<yacl::link::Context> lctx);
  ~BucketPsi() = default;

  PsiResultReport Run(ProgressCallbacks progress_callbacks = nullptr,
                      int64_t callbacks_interval_ms = 5 * 1000);

  // unbalanced get items_count when RunPSI
  // other psi use sanity check get items_count
  // TODO: sanity check affects performance maybe optional
  std::vector<uint64_t> RunPsi(std::shared_ptr<Progress>& progress,
                               uint64_t& self_items_count);

  void ProduceOutput(bool digest_equal, std::vector<uint64_t>& indices,
                     PsiResultReport& report);

 private:
  void Init();

  std::vector<uint64_t> RunBucketPsi(std::shared_ptr<Progress>& progress,
                                     uint64_t self_items_count);

  BucketPsiConfig config_;

  std::shared_ptr<yacl::link::Context> lctx_;

  std::vector<std::string> selected_fields_;

  std::unique_ptr<MemoryPsi> mem_psi_;
};

}  // namespace psi
