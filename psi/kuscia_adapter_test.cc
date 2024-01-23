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

#include <gtest/gtest.h>

#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

#include "psi/proto/psi_v2.pb.h"

namespace psi {

TEST(KusicaAdapterTest, works) {
  constexpr auto kuscia_json = R"json(
{
  "task_id": "task_id_1234",
  "task_input_config": "{\n    \"sf_psi_config_map\": {\n        \"alice\": {\n            \"psi_config\": {\n                \"protocolConfig\": {\n                    \"protocol\": \"PROTOCOL_ECDH\",\n                    \"ecdhConfig\": {\n                        \"curve\": \"CURVE_25519\"\n                    },\n                    \"role\": \"ROLE_RECEIVER\",\n                    \"broadcastResult\": true\n                },\n                \"inputConfig\": {\n                    \"type\": \"IO_TYPE_FILE_CSV\",\n                    \"path\": \"/tmp/receiver_input.csv\"\n                },\n                \"outputConfig\": {\n                    \"type\": \"IO_TYPE_FILE_CSV\",\n                    \"path\": \"/tmp/receiver_output.csv\"\n                },\n                \"keys\": [\n                    \"id0\",\n                    \"id1\"\n                ],\n                \"debugOptions\": {\n                    \"tracePath\": \"/tmp/receiver.trace\"\n                },\n                \"recoveryConfig\": {\n                    \"enabled\": true,\n                    \"folder\": \"/tmp/receiver_cache\"\n                }\n            },\n            \"linkConfig\": {\n                \"recv_timeout_ms\": 180000,\n                \"http_timeout_ms\": 120000\n            }\n        },\n        \"bob\": {\n            \"psi_config\": {\n                \"protocolConfig\": {\n                    \"protocol\": \"PROTOCOL_ECDH\",\n                    \"ecdhConfig\": {\n                        \"curve\": \"CURVE_25519\"\n                    },\n                    \"role\": \"ROLE_SENDER\",\n                    \"broadcastResult\": true\n                },\n                \"inputConfig\": {\n                    \"type\": \"IO_TYPE_FILE_CSV\",\n                    \"path\": \"/tmp/sender_input.csv\"\n                },\n                \"outputConfig\": {\n                    \"type\": \"IO_TYPE_FILE_CSV\",\n                    \"path\": \"/tmp/sender_output.csv\"\n                },\n                \"keys\": [\n                    \"id0\",\n                    \"id1\"\n                ],\n                \"debugOptions\": {\n                    \"tracePath\": \"/tmp/sender.trace\"\n                },\n                \"recoveryConfig\": {\n                    \"enabled\": true,\n                    \"folder\": \"/tmp/sender_cache\"\n                }\n            },\n            \"linkConfig\": {\n                \"recv_timeout_ms\": 180000,\n                \"http_timeout_ms\": 120000\n            }\n        }\n    }\n}",
  "task_cluster_def": "{\n        \"parties\": [\n            {\n                \"name\": \"alice\",\n                \"services\": [\n                    {\n                        \"portName\": \"psi\",\n                        \"endpoints\": [\n                            \"1.2.3.4:1234\"\n                        ]\n                    }\n                ]\n            },\n            {\n                \"name\": \"bob\",\n                \"services\": [\n                    {\n                        \"portName\": \"psi\",\n                        \"endpoints\": [\n                            \"1.2.3.5:1235\"\n                        ]\n                    }\n                ]\n            }\n        ],\n        \"self_party_idx\": 0\n    }",
  "allocated_ports": "{\n        \"ports\": [\n            {\n                \"name\": \"psi\",\n                \"port\": 1234\n            }\n        ]\n    }"
})json";

  KusciaTask task = FromKusciaConfig(kuscia_json);

  constexpr auto expected_config_json = R"json(
  {
    "psi_config": {
        "protocolConfig": {
            "protocol": "PROTOCOL_ECDH",
            "ecdhConfig": {
                "curve": "CURVE_25519"
            },
            "role": "ROLE_RECEIVER",
            "broadcastResult": true
        },
        "inputConfig": {
            "type": "IO_TYPE_FILE_CSV",
            "path": "/tmp/receiver_input.csv"
        },
        "outputConfig": {
            "type": "IO_TYPE_FILE_CSV",
            "path": "/tmp/receiver_output.csv"
        },
        "keys": [
            "id0",
            "id1"
        ],
        "debugOptions": {
            "tracePath": "/tmp/receiver.trace"
        },
        "recoveryConfig": {
            "enabled": true,
            "folder": "/tmp/receiver_cache"
        }
    },
    "linkConfig": {
        "parties": [
            {
                "id": "alice",
                "host": "0.0.0.0:1234"
            },
            {
                "id": "bob",
                "host": "http://1.2.3.5:1235"
            }
        ],
        "brpc_channel_protocol": "http",
        "brpc_channel_connection_type": "pooled",
        "recv_timeout_ms": 180000,
        "http_timeout_ms": 120000
    },
    "selfLinkParty": "alice"
  })json";

  LaunchConfig expected_config;
  EXPECT_TRUE(google::protobuf::util::JsonStringToMessage(expected_config_json,
                                                          &expected_config)
                  .ok());

  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_config, task.launch_config));
}

}  // namespace psi
