#!/bin/bash

set -ex

cp -r src src_copied
cd src_copied

conda install -y perl=5.20.3.1
conda update -y sysroot_linux-64



bazel build psi/apps/psi_launcher:main -c opt --config=linux-release --repository_cache=/tmp/bazel_repo_cache --remote_timeout=300s --remote_retries=10
chmod 777 bazel-bin/psi/apps/psi_launcher/main
mkdir -p ../src/docker/linux/amd64
cp bazel-bin/psi/apps/psi_launcher/main ../src/docker/linux/amd64
