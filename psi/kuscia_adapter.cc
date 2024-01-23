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

#include "psi/kuscia_adapter.h"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>

#include "google/protobuf/util/json_util.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "yacl/base/exception.h"

#include "psi/proto/kuscia.pb.h"

namespace psi {

KusciaTask FromKusciaConfig(const std::string& json_str) {
  rapidjson::Document d;
  d.Parse(json_str.c_str());

  KusciaTask kuscia_task;

  kuscia_task.task_id = d["task_id"].GetString();

  std::string cluster_def_str = {d["task_cluster_def"].GetString(),
                                 d["task_cluster_def"].GetStringLength()};

  kuscia::ClusterDefine task_cluster_def;
  YACL_ENFORCE(google::protobuf::util::JsonStringToMessage(cluster_def_str,
                                                           &task_cluster_def)
                   .ok());

  std::string self_party =
      task_cluster_def.parties(task_cluster_def.self_party_idx()).name();

  kuscia::TaskInputConfig task_input_config;

  std::string task_input_config_str = {
      d["task_input_config"].GetString(),
      d["task_input_config"].GetStringLength()};

  YACL_ENFORCE(google::protobuf::util::JsonStringToMessage(
                   task_input_config_str, &task_input_config)
                   .ok());

  kuscia_task.launch_config =
      task_input_config.sf_psi_config_map().at(self_party);

  kuscia::AllocatedPorts allocated_ports;
  std::string allocated_ports_str = {d["allocated_ports"].GetString(),
                                     d["allocated_ports"].GetStringLength()};
  YACL_ENFORCE(google::protobuf::util::JsonStringToMessage(allocated_ports_str,
                                                           &allocated_ports)
                   .ok());

  size_t port = allocated_ports.ports(0).port();
  std::map<std::string, std::string> id_host_map;

  for (int i = 0; i < task_cluster_def.parties().size(); i++) {
    const auto& party = task_cluster_def.parties(i);

    id_host_map[party.name()] =
        i == task_cluster_def.self_party_idx()
            ? fmt::format("0.0.0.0:{}", port)
            : fmt::format("http://{}", party.services(0).endpoints(0));
  }

  for (auto const& [id, host] : id_host_map) {
    auto* new_party =
        kuscia_task.launch_config.mutable_link_config()->add_parties();
    new_party->set_host(host);
    new_party->set_id(id);
  }

  kuscia_task.launch_config.mutable_link_config()
      ->set_brpc_channel_connection_type("pooled");
  kuscia_task.launch_config.mutable_link_config()->set_brpc_channel_protocol(
      "http");
  kuscia_task.launch_config.set_self_link_party(self_party);

  return kuscia_task;
}

}  // namespace psi