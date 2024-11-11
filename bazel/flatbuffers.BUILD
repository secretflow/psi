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

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "FlatBuffers",
    cache_entries = {
        "FLATBUFFERS_BUILD_TESTS": "OFF",
        "CMAKE_EXE_LINKER_FLAGS": "-lm",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    generate_args = ["-GNinja"],
    lib_source = ":all_srcs",
    out_binaries = ["flatc"],
    out_static_libs = ["libflatbuffers.a"],
)
