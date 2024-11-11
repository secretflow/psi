#!/bin/bash

set -ex

cp -r src src_copied
cd src_copied

bazel build psi:main -c opt --config=linux-release --repository_cache=/tmp/bazel_repo_cache
chmod 777 bazel-bin/psi/main
mkdir -p ../src/docker/linux/amd64
cp bazel-bin/psi/main ../src/docker/linux/amd64
