# Copyright 2024 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@psi//bazel:psi.bzl", "psi_cmake_external")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["**"]),
)

psi_cmake_external(
    name = "cppzmq",
    cache_entries = {
        "CPPZMQ_BUILD_TESTS": "OFF",
    },
    lib_source = "@com_github_zeromq_cppzmq//:all",
    out_headers_only = True,
    deps = [
        "@com_github_zeromq_libzmq//:ZeroMQ",
    ],
)
