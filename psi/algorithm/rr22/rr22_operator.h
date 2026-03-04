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

#include "psi/algorithm/psi_operator.h"
#include "psi/algorithm/rr22/rr22_psi.h"

namespace psi::rr22 {

class Rr22Operator : public PsiOperator {
 public:
  struct Options {
    Rr22PsiOptions rr22_opts;
    size_t receiver_rank;
    std::shared_ptr<yacl::link::Context> lctx;
    bool broadcast_result = false;
    bool pipeline_mode = true;
    // enabled when `pipeline_mode = false`
    size_t parallel_level = 6;

    std::shared_ptr<RecoveryManager> recovery_manager = nullptr;
    std::string cache_dir =
        std::filesystem::temp_directory_path() / GetRandomString();
  };

 public:
  explicit Rr22Operator(Options opts, std::shared_ptr<IDataStore> input_store,
                        std::shared_ptr<IResultStore> output_store);
  ~Rr22Operator() = default;

  bool SupportStreaming() override { return false; }

  bool SupportBucketParallel() override { return true; }

  bool ReceiveResult() override;

 protected:
  void OnInit() override;

  void OnRun() override;

  void OnEnd() override;

 private:
  Options opts_;
};

}  // namespace psi::rr22
