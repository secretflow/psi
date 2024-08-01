# Copyright 2022 Ant Group Co., Ltd.
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
    name = "apsi",
    cache_entries = {
        "APSI_USE_LOG4CPLUS": "OFF",
        "APSI_USE_ZMQ": "ON",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "EXT_BUILD_DEPS": "$EXT_BUILD_DEPS",
    },
    lib_source = "@com_github_microsoft_apsi//:all",
    out_include_dir = "include/APSI-0.11",
    out_static_libs = ["libapsi-0.11.a"],
    deps = [
        "@com_github_facebook_zstd//:zstd",
        # "@com_github_log4cplus_log4cplus//:log4cplus",
        "@com_github_microsoft_FourQlib//:FourQlib",
        "@com_github_microsoft_gsl//:Microsoft.GSL",
        "@com_github_microsoft_kuku//:kuku",
        "@com_github_microsoft_seal//:seal",
        "@com_github_open_source_parsers_jsoncpp//:jsoncpp",
        "@com_github_zeromq_cppzmq//:cppzmq",
        "@com_google_flatbuffers//:FlatBuffers",
        "@zlib",
    ],
)
