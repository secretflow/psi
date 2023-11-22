#!/bin/bash

set -ex

cp -r src src_copied
cd src_copied




bazel build psi/psi:main -c opt --config=linux-release --repository_cache=/tmp/bazel_repo_cache
chmod 777 bazel-bin/psi/psi/main
cp bazel-bin/psi/psi/main ../src/docker/
