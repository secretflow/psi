#!/bin/bash

set -ex

cp -r src src_copied
cd src_copied

# OPENSOURCE-CLEANUP REMOVE BEGIN
# fill in

# OPENSOURCE-CLEANUP REMOVE END

bazel build psi:main -c opt --config=linux-release --repository_cache=/tmp/bazel_repo_cache
bazel build psi/apsi_wrapper/api:wrapper_shared -c opt --config=linux-release --repository_cache=/tmp/bazel_repo_cache

chmod 777 bazel-bin/psi/main
chmod 777 bazel-bin/psi/apsi_wrapper/api/wrapper.so
mkdir -p ../src/psi/apsi_wrapper/docker/linux/amd64
cp bazel-bin/psi/main ../src/psi/apsi_wrapper/docker/linux/amd64
cp bazel-bin/psi/apsi_wrapper/api/wrapper.so ../src/psi/apsi_wrapper/docker/linux/amd64
