# SecretFlow PSI Library

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/secretflow/psi/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/secretflow/psi/tree/main)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/secretflow/psi/badge)](https://securityscorecards.dev/viewer/?uri=github.com/secretflow/psi)

The repo of Private Set Intersection(PSI) and Private Information Retrieval(PIR) from SecretFlow.

This repo is formerly psi/pir part from secretflow/spu repo.

> **Note**<br>
We invite you to try [Easy PSI](https://www.secretflow.org.cn/zh-CN/docs/easy-psi/), a standalone PSI product powered by this library.

## PSI Quick Start with v2 API

For PSI v1 API and PIR, please check [documentation](https://www.secretflow.org.cn/docs/psi).

### Release Docker

In the following example, we are going to run PSI at a single host.

1. Check official release docker image at [dockerhub](https://hub.docker.com/r/secretflow/psi-anolis8). We also have mirrors at Alibaba Cloud: secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8.

2. Prepare data and config.

    receiver.config:

    ```json
    {
        "psi_config": {
            "protocol_config": {
                "protocol": "PROTOCOL_RR22",
                "role": "ROLE_RECEIVER",
                "broadcast_result": true
            },
            "input_config": {
                "type": "IO_TYPE_FILE_CSV",
                "path": "/root/receiver/receiver_input.csv"
            },
            "output_config": {
                "type": "IO_TYPE_FILE_CSV",
                "path": "/root/receiver/receiver_output.csv"
            },
            "keys": [
                "id0",
                "id1"
            ],
            "debug_options": {
                "trace_path": "/root/receiver/receiver.trace"
            }
        },
        "self_link_party": "receiver",
        "link_config": {
            "parties": [
                {
                    "id": "receiver",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "sender",
                    "host": "127.0.0.1:5400"
                }
            ]
        }
    }
    ```

    sender.config:

    ```json
    {
        "psi_config": {
            "protocol_config": {
                "protocol": "PROTOCOL_RR22",
                "role": "ROLE_SENDER",
                "broadcast_result": true
            },
            "input_config": {
                "type": "IO_TYPE_FILE_CSV",
                "path": "/root/sender/sender_input.csv"
            },
            "output_config": {
                "type": "IO_TYPE_FILE_CSV",
                "path": "/root/sender/sender_output.csv"
            },
            "keys": [
                "id0",
                "id1"
            ],
            "debug_options": {
                "trace_path": "/root/sender/sender.trace"
            }
        },
        "self_link_party": "sender",
        "link_config": {
            "parties": [
                {
                    "id": "receiver",
                    "host": "127.0.0.1:5300"
                },
                {
                    "id": "sender",
                    "host": "127.0.0.1:5400"
                }
            ]
        }
    }
    ```

    | File Name          | Location                            | Description                                                                |
    | :----------------  | :---------------------------------- | :------------------------------------------------------------------------- |
    | receiver.config    | /tmp/receiver/receiver.config       | Config for receiver.                                                       |
    | sender.config      | /tmp/sender/sender.config           | Config for sender.                                                         |
    | receiver_input.csv | /tmp/receiver/receiver_input.csv | Input for receiver. Make sure the file contains two id keys - id0 and id1. |
    | sender_input.csv   | /tmp/sender/sender_input.csv     | Input for sender. Make sure the file contains two id keys - id0 and id1.   |

3. Run PSI

In the first terminal, run the following command

```bash
docker run -it  --rm  --network host --mount type=bind,source=/tmp/receiver,target=/root/receiver --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:latest --config receiver/receiver.config
```

In the other terminal, run the following command simultaneously.

```bash
docker run -it  --rm  --network host --mount type=bind,source=/tmp/sender,target=/root/sender  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:latest --config sender/sender.config
```

You could also pass a minified JSON config directly. A minified JSON is a compact one without white space and line breaks.

e.g.

```bash
docker run -it  --rm  --network host --mount type=bind,source=/tmp/sender,target=/root/sender  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/psi-anolis8:latest --json '{"psi_config":{"protocol_config":{"protocol":"PROTOCOL_RR22","role":"ROLE_RECEIVER","broadcast_result":true},"input_config":{"type":"IO_TYPE_FILE_CSV","path":"/root/receiver/receiver_input.csv"},"output_config":{"type":"IO_TYPE_FILE_CSV","path":"/root/receiver/receiver_output.csv"},"keys":["id0","id1"],"debug_options":{"trace_path":"/root/receiver/receiver.trace"}},"self_link_party":"receiver","link_config":{"parties":[{"id":"receiver","host":"127.0.0.1:5300"},{"id":"sender","host":"127.0.0.1:5400"}]}}'
```

## Building SecretFlow PSI Library

### System Setup


#### Dev Docker

We use secretflow/ubuntu-base-ci docker image. You may check at [dockerhub](https://hub.docker.com/r/secretflow/ubuntu-base-ci).

```sh
# start container
docker run -d -it --name psi-dev-$(whoami) \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --privileged=true \
         --entrypoint="bash" \
         secretflow/ubuntu-base-ci:latest

# attach to build container
docker exec -it psi-dev-$(whoami) bash
```

#### Linux

```sh
Install gcc>=11.2, cmake>=3.26, ninja, nasm>=2.15, python>=3.8, bazel, golang, xxd, lld
```

> **Note**<br>
Please install bazel with version in .bazeliskrc or use bazelisk.

### Build & UnitTest




``` sh
# build as debug
bazel build //... -c dbg

# build as release
bazel build //... -c opt

# test
bazel test //...
```

### Trace

We use [Perfetto](https://perfetto.dev/) from Google for tracing.

Please use debug_options.trace_path field in PsiConfig to modify trace file path. The default path is /tmp/psi.trace.

After running psi binaries, please check trace by using [Trace Viewer](https://ui.perfetto.dev/). If this is not applicable,
please check [this link](https://github.com/google/perfetto/issues/170) to deploy your own website.

The alternate way to visualize trace is to use **chrome://tracing**:

1. Download perfetto assets from <https://github.com/google/perfetto/releases/tag/v37.0>
2. You should find traceconv binary in assets folder.
3. Transfer trace file to JSON format:

    ```bash
    chmod +x traceconv

    ./traceconv json [trace file path] [json file path]
    ```

4. Open chrome://tracing in your chrome and load JSON file.





## PSI V2 Benchamrk

Please refer to [PSI V2 Benchmark](docs/user_guide/psi_v2_benchmark.md)

## APSI Benchmark

Please refer to [APSI Benchmark](docs/user_guide/apsi_benchmark.md)
