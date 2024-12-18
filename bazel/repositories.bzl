# Copyright 2021 Ant Group Co., Ltd.
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

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def psi_deps():
    _com_github_facebook_zstd()
    _com_github_microsoft_seal()
    _com_github_microsoft_apsi()
    _com_github_microsoft_gsl()
    _com_github_microsoft_kuku()
    _com_google_flatbuffers()

    _com_github_google_perfetto()
    _com_github_floodyberry_curve25519_donna()

    _com_github_zeromq_cppzmq()
    _com_github_zeromq_libzmq()
    _com_github_log4cplus_log4cplus()
    _com_github_open_source_parsers_jsoncpp()

def _com_github_facebook_zstd():
    maybe(
        http_archive,
        name = "zstd",
        build_file = "//bazel:zstd.BUILD",
        strip_prefix = "zstd-1.5.6",
        sha256 = "30f35f71c1203369dc979ecde0400ffea93c27391bfd2ac5a9715d2173d92ff7",
        type = ".tar.gz",
        urls = [
            "https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz",
        ],
    )

def _com_github_microsoft_seal():
    maybe(
        http_archive,
        name = "seal",
        sha256 = "af9bf0f0daccda2a8b7f344f13a5692e0ee6a45fea88478b2b90c35648bf2672",
        strip_prefix = "SEAL-4.1.1",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@psi//bazel/patches:seal.patch"],
        urls = [
            "https://github.com/microsoft/SEAL/archive/refs/tags/v4.1.1.tar.gz",
        ],
        build_file = "@psi//bazel:seal.BUILD",
    )

def _com_github_microsoft_apsi():
    maybe(
        http_archive,
        name = "apsi",
        sha256 = "82c0f9329c79222675109d4a3682d204acd3ea9a724bcd98fa58eabe53851333",
        strip_prefix = "APSI-0.11.0",
        urls = [
            "https://github.com/microsoft/APSI/archive/refs/tags/v0.11.0.tar.gz",
        ],
        build_file = "@psi//bazel:microsoft_apsi.BUILD",
        patch_args = ["-p1"],
        patches = [
            "@psi//bazel/patches:apsi.patch",
            "@psi//bazel/patches:apsi-fourq.patch",
        ],
        patch_cmds = [
            "rm -rf common/apsi/fourq",
        ],
    )

def _com_github_microsoft_gsl():
    maybe(
        http_archive,
        name = "com_github_microsoft_gsl",
        sha256 = "f0e32cb10654fea91ad56bde89170d78cfbf4363ee0b01d8f097de2ba49f6ce9",
        strip_prefix = "GSL-4.0.0",
        type = "tar.gz",
        urls = [
            "https://github.com/microsoft/GSL/archive/refs/tags/v4.0.0.tar.gz",
        ],
        build_file = "@psi//bazel:microsoft_gsl.BUILD",
    )

def _com_github_microsoft_kuku():
    maybe(
        http_archive,
        name = "kuku",
        sha256 = "96ed5fad82ea8c8a8bb82f6eaf0b5dce744c0c2566b4baa11d8f5443ad1f83b7",
        strip_prefix = "Kuku-2.1.0",
        type = "tar.gz",
        urls = [
            "https://github.com/microsoft/Kuku/archive/refs/tags/v2.1.0.tar.gz",
        ],
        build_file = "@psi//bazel:microsoft_kuku.BUILD",
    )

def _com_google_flatbuffers():
    maybe(
        http_archive,
        name = "com_google_flatbuffers",
        sha256 = "4157c5cacdb59737c5d627e47ac26b140e9ee28b1102f812b36068aab728c1ed",
        strip_prefix = "flatbuffers-24.3.25",
        urls = [
            "https://github.com/google/flatbuffers/archive/refs/tags/v24.3.25.tar.gz",
        ],
        patch_cmds = [
            # hack to make sure this file is removed
            "rm grpc/BUILD.bazel",
            "rm grpc/src/compiler/BUILD.bazel",
            "rm src/BUILD.bazel",
        ],
        build_file = "@psi//bazel:flatbuffers.BUILD",
    )

def _com_github_google_perfetto():
    maybe(
        http_archive,
        name = "perfetto",
        urls = [
            "https://github.com/google/perfetto/archive/refs/tags/v41.0.tar.gz",
        ],
        sha256 = "4c8fe8a609fcc77ca653ec85f387ab6c3a048fcd8df9275a1aa8087984b89db8",
        strip_prefix = "perfetto-41.0",
        patch_args = ["-p1"],
        patches = ["@psi//bazel/patches:perfetto.patch"],
        build_file = "@psi//bazel:perfetto.BUILD",
    )

def _com_github_floodyberry_curve25519_donna():
    maybe(
        http_archive,
        name = "curve25519-donna",
        strip_prefix = "curve25519-donna-2fe66b65ea1acb788024f40a3373b8b3e6f4bbb2",
        sha256 = "ba57d538c241ad30ff85f49102ab2c8dd996148456ed238a8c319f263b7b149a",
        type = "tar.gz",
        build_file = "@psi//bazel:curve25519-donna.BUILD",
        urls = [
            "https://github.com/floodyberry/curve25519-donna/archive/2fe66b65ea1acb788024f40a3373b8b3e6f4bbb2.tar.gz",
        ],
    )

def _com_github_zeromq_cppzmq():
    maybe(
        http_archive,
        name = "com_github_zeromq_cppzmq",
        build_file = "@psi//bazel:cppzmq.BUILD",
        strip_prefix = "cppzmq-4.10.0",
        sha256 = "c81c81bba8a7644c84932225f018b5088743a22999c6d82a2b5f5cd1e6942b74",
        type = ".tar.gz",
        urls = [
            "https://github.com/zeromq/cppzmq/archive/refs/tags/v4.10.0.tar.gz",
        ],
    )

def _com_github_zeromq_libzmq():
    maybe(
        http_archive,
        name = "com_github_zeromq_libzmq",
        build_file = "@psi//bazel:libzmq.BUILD",
        strip_prefix = "libzmq-4.3.5",
        sha256 = "6c972d1e6a91a0ecd79c3236f04cf0126f2f4dfbbad407d72b4606a7ba93f9c6",
        type = ".tar.gz",
        urls = [
            "https://github.com/zeromq/libzmq/archive/refs/tags/v4.3.5.tar.gz",
        ],
    )

def _com_github_log4cplus_log4cplus():
    maybe(
        http_archive,
        name = "com_github_log4cplus_log4cplus",
        build_file = "@psi//bazel:log4cplus.BUILD",
        strip_prefix = "log4cplus-2.1.1",
        sha256 = "42dc435928917fd2f847046c4a0c6086b2af23664d198c7fc1b982c0bfe600c1",
        type = ".tar.gz",
        urls = [
            "https://github.com/log4cplus/log4cplus/releases/download/REL_2_1_1/log4cplus-2.1.1.tar.gz",
        ],
    )

def _com_github_open_source_parsers_jsoncpp():
    maybe(
        http_archive,
        name = "com_github_open_source_parsers_jsoncpp",
        build_file = "@psi//bazel:jsoncpp.BUILD",
        strip_prefix = "jsoncpp-1.9.6",
        sha256 = "f93b6dd7ce796b13d02c108bc9f79812245a82e577581c4c9aabe57075c90ea2",
        type = ".tar.gz",
        urls = [
            "https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.6.tar.gz",
        ],
    )
