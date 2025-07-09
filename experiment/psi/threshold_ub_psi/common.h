// Copyright 2025
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

#include <filesystem>
#include <fstream>
#include <string>

#include "psi/proto/psi_v2.pb.h"

namespace psi::ecdh {

// The client or server may need to know the size of the real intersection and
// the size of the intersection after being restricted by threshold.
void SaveIntersectCount(const std::string &count_path, uint32_t real_count,
                        uint32_t final_count);

// The following methods are used for test and benchmark

// Write test data to a csv file
void WriteCsvFile(const std::string &file_path,
                  const std::vector<std::string> &items);

// Generate UbPsiConfig for test and benchmark
void GenerateUbPsiConfig(const std::filesystem::path &tmp_folder,
                         const std::vector<std::string> &items_server,
                         const std::vector<std::string> &items_client,
                         uint32_t threshold, v2::UbPsiConfig &server_config,
                         v2::UbPsiConfig &client_config);
}  // namespace psi::ecdh