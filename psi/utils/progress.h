// Copyright 2023 Ant Group Co., Ltd.
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

#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace psi {

class Progress {
 public:
  enum class Mode : uint8_t {
    kSingle,
    kSerial,
    kParallel,
  };

  struct Data {
    size_t total{0};
    size_t finished{0};
    size_t running{0};
    size_t percentage{0};
    std::string description;
  };

 public:
  explicit Progress(std::string description = "");

  ~Progress() = default;

  void Update(size_t percentage);

  Data Get();

  void Done();

  bool IsDone() const;

  void SetWeights(std::vector<size_t> weights, Mode mode = Mode::kSerial);

  void SetSubJobCount(size_t count, Mode mode = Mode::kParallel);

  std::shared_ptr<Progress> AddSubProgress(const std::string& description = "");

  // Mark current sub progress as Done and Add new sub progress
  std::shared_ptr<Progress> NextSubProgress(
      const std::string& description = "");

 private:
  std::shared_mutex rw_mutex_;

  std::vector<size_t> weights_;

  std::vector<std::shared_ptr<Progress>> sub_progresses_;

  const std::string description_;

  std::atomic_size_t percentage_;

  std::atomic<Mode> mode_;

  std::atomic_bool done_;
};

using ProgressCallbacks = std::function<void(const Progress::Data&)>;
}  // namespace psi
