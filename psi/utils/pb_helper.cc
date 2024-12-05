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

#include "psi/utils/pb_helper.h"

#include <filesystem>
#include <fstream>

#include "google/protobuf/util/json_util.h"
#include "yacl/base/exception.h"

#include "psi/utils/io.h"

namespace psi {

void DumpPbMessageToJsonFile(google::protobuf::Message& msg,
                             const std::string& json_file) {
  std::string json;
  auto stat = ::google::protobuf::util::MessageToJsonString(msg, &json);
  YACL_ENFORCE(stat.ok(), "pb to json failed, status:{}", stat.ToString());

  auto ofs = io::GetStdOutFileStream(json_file);
  *ofs << json;
  YACL_ENFORCE(ofs->good(), "save json file {} failed.", json_file);
}

void LoadJsonFileToPbMessage(const std::string& json_file,
                             google::protobuf::Message& msg) {
  YACL_ENFORCE(std::filesystem::exists(json_file), "json file: {} not exists.",
               json_file);
  std::ifstream ifs(json_file);
  std::string json;
  std::string line;
  while (std::getline(ifs, line)) {
    json += line;
  }
  auto stat = ::google::protobuf::util::JsonStringToMessage(json, &msg);
  YACL_ENFORCE(stat.ok(), "json file: {} to pb failed, status:{}", json_file,
               stat.ToString());
}
}  // namespace psi
