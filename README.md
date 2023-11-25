# SecretFlow PSI Library

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/secretflow/psi/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/secretflow/psi/tree/main)

The repo of Private Set Intersection(PSI) and Private Information Retrieval(PIR) from SecretFlow.

This repo is formerly psi/pir part from secretflow/spu repo.

## Quick Start

<!-- todo -->

## Building SecretFlow PSI Library

### System Setup


#### Docker

We use the same dev docker from secretflow/ubuntu-base-ci.

```sh
## start container
docker run -d -it --name psi-dev-$(whoami) \
         --mount type=bind,source="$(pwd)",target=/home/admin/dev/ \
         -w /home/admin/dev \
         --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
         --cap-add=NET_ADMIN \
         --privileged=true \
         secretflow/ubuntu-base-ci:latest

# attach to build container
docker exec -it psi-dev-$(whoami) bash
```

#### Linux

```sh
Install gcc>=11.2, cmake>=3.26, ninja, nasm>=2.15, python>=3.8, bazel==6.2.1, golang, xxd, lld
```

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

After running psi binaries, please check trace by using [Trace Viewer](https://ui.perfetto.dev/). If this is not applicable, please check [this link](https://github.com/google/perfetto/issues/170) to deploy your own website.

The alternate way to visualize trace is to use **chrome://tracing**:
1. Download perfetto assets from https://github.com/google/perfetto/releases/tag/v37.0
2. You should find traceconv binary in assets folder.
3. Transfer trace file to JSON format:

```bash
chmod +x traceconv

./traceconv json [trace file path] [json file path]
```
4. Open chrome://tracing in your chrome and load JSON file.
