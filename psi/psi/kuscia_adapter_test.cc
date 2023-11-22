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

#include "psi/psi/kuscia_adapter.h"

#include <gtest/gtest.h>

#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

#include "psi/proto/psi.pb.h"

namespace psi::psi {

TEST(KusicaAdapterTest, works) {
  constexpr auto kuscia_json = R"json(
{
  "task_id": "task_id_1234",
  "task_input_config": "{\n    \"sf_psi_config_map\": {\n        \"alice\": {\n            \"protocol_config\": {\n                \"protocol\": \"PROTOCOL_ECDH\",\n                \"ecdh_config\": {\n                    \"curve\": \"CURVE_25519\"\n                },\n                \"role\": \"ROLE_RECEIVER\",\n                \"broadcast_result\": true\n            },\n            \"input_config\": {\n                \"type\": \"IO_TYPE_FILE_CSV\",\n                \"path\": \"/tmp/receiver_input.csv\"\n            },\n            \"output_config\": {\n                \"type\": \"IO_TYPE_FILE_CSV\",\n                \"path\": \"/tmp/receiver_output.csv\"\n            },\n            \"keys\": [\n                \"id0\",\n                \"id1\"\n            ],\n            \"debug_options\": {\n                \"trace_path\": \"/tmp/receiver.trace\"\n            },\n            \"check_duplicates\": false,\n            \"sort_output\": false,\n            \"recovery_config\": {\n                \"enabled\": true,\n                \"folder\": \"/tmp/receiver_cache\"\n            },\n            \"link_config\": {\n                \"recv_timeout_ms\": 180000,\n                \"http_timeout_ms\": 120000\n            }\n        },\n        \"bob\": {\n            \"protocol_config\": {\n                \"protocol\": \"PROTOCOL_ECDH\",\n                \"ecdh_config\": {\n                    \"curve\": \"CURVE_25519\"\n                },\n                \"role\": \"ROLE_SENDER\",\n                \"broadcast_result\": true\n            },\n            \"input_config\": {\n                \"type\": \"IO_TYPE_FILE_CSV\",\n                \"path\": \"/tmp/sender_input.csv\"\n            },\n            \"output_config\": {\n                \"type\": \"IO_TYPE_FILE_CSV\",\n                \"path\": \"/tmp/sender_output.csv\"\n            },\n            \"keys\": [\n                \"id2\",\n                \"id3\"\n            ],\n            \"debug_options\": {\n                \"trace_path\": \"/tmp/sender.trace\"\n            },\n            \"check_duplicates\": false,\n            \"sort_output\": false,\n            \"recovery_config\": {\n                \"enabled\": true,\n                \"folder\": \"/tmp/sender_cache\"\n            },\n            \"link_config\": {\n                \"recv_timeout_ms\": 180000,\n                \"http_timeout_ms\": 120000\n            }\n        }\n    }\n}",
  "task_cluster_def": "{\n        \"parties\": [\n            {\n                \"name\": \"alice\",\n                \"services\": [\n                    {\n                        \"portName\": \"psi\",\n                        \"endpoints\": [\n                            \"1.2.3.4:1234\"\n                        ]\n                    }\n                ]\n            },\n            {\n                \"name\": \"bob\",\n                \"services\": [\n                    {\n                        \"portName\": \"psi\",\n                        \"endpoints\": [\n                            \"1.2.3.5:1235\"\n                        ]\n                    }\n                ]\n            }\n        ],\n        \"self_party_idx\": 0\n    }",
  "allocated_ports": "{\n        \"ports\": [\n            {\n                \"name\": \"psi\",\n                \"port\": 1234\n            }\n        ]\n    }"
})json";

  KusciaTask task = FromKusciaConfig(kuscia_json);

  constexpr auto expected_psi_json = R"json(
  {
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
      "selfLinkParty": "alice",
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
  })json";

  v2::PsiConfig expected_psi_config;
  EXPECT_TRUE(google::protobuf::util::JsonStringToMessage(expected_psi_json,
                                                          &expected_psi_config)
                  .ok());

  EXPECT_TRUE(::google::protobuf::util::MessageDifferencer::Equals(
      expected_psi_config, task.psi_config));
}

}  // namespace psi::psi