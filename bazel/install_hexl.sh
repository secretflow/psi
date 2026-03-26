#!/usr/bin/env bash

set -euo pipefail

hexl_version="${HEXL_VERSION:-v1.2.6}"
install_prefix="${1:-${HEXL_ROOT:-${HOME}/.local/hexl}}"

if [[ -f "${install_prefix}/lib/libhexl.a" && -d "${install_prefix}/include/hexl" ]]; then
  printf 'Intel HEXL already installed at %s\n' "${install_prefix}"
  exit 0
fi

parallelism="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
workdir="$(mktemp -d)"
repo_dir="${workdir}/hexl"
build_dir="${workdir}/build"

cleanup() {
  rm -rf "${workdir}"
}

trap cleanup EXIT

printf 'Installing Intel HEXL %s into %s\n' "${hexl_version}" "${install_prefix}"

git clone --depth 1 --branch "${hexl_version}"   https://github.com/IntelLabs/hexl.git "${repo_dir}"

cmake -S "${repo_dir}" -B "${build_dir}"   -DCMAKE_BUILD_TYPE=Release   -DCMAKE_INSTALL_PREFIX="${install_prefix}"   -DHEXL_BENCHMARK=OFF   -DHEXL_COVERAGE=OFF   -DHEXL_DOCS=OFF   -DHEXL_SHARED_LIB=OFF   -DHEXL_TESTING=OFF   -DHEXL_TREAT_WARNING_AS_ERROR=OFF

cmake --build "${build_dir}" --parallel "${parallelism}"
cmake --install "${build_dir}"
