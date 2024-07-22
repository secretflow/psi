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
    _com_github_nelhage_rules_boost()
    _bazel_platform()
    _upb()
    _com_github_emptoolkit_emp_tool()
    _com_github_emptoolkit_emp_ot()
    _com_github_emptoolkit_emp_zk()
    _com_github_facebook_zstd()
    _com_github_microsoft_seal()
    _com_github_eigenteam_eigen()
    _com_github_microsoft_apsi()
    _com_github_microsoft_gsl()
    _com_github_microsoft_kuku()
    _com_google_flatbuffers()
    _org_apache_arrow()
    _com_github_grpc_grpc()
    _com_github_tencent_rapidjson()
    _com_github_xtensor_xsimd()
    _brotli()
    _com_github_lz4_lz4()
    _org_apache_thrift()
    _com_google_double_conversion()
    _bzip2()
    _com_github_google_snappy()
    _com_github_google_perfetto()
    _com_github_floodyberry_curve25519_donna()
    _com_github_ridiculousfish_libdivide()
    _com_github_sparsehash_sparsehash()
    _com_github_intel_ipp()
    _yacl()

def _yacl():
    maybe(
        http_archive,
        name = "yacl",
        urls = [
            "https://github.com/secretflow/yacl/archive/refs/tags/0.4.5b3_nightly_20240722.tar.gz",
        ],
        strip_prefix = "yacl-0.4.5b3_nightly_20240722",
        sha256 = "ccca599e6ded6089c5afbb87c8f5e09383195af256caacd50089f0c7443e8604",
    )

def _bazel_platform():
    http_archive(
        name = "platforms",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/0.0.8/platforms-0.0.8.tar.gz",
            "https://github.com/bazelbuild/platforms/releases/download/0.0.8/platforms-0.0.8.tar.gz",
        ],
        sha256 = "8150406605389ececb6da07cbcb509d5637a3ab9a24bc69b1101531367d89d74",
    )

def _com_github_facebook_zstd():
    maybe(
        http_archive,
        name = "com_github_facebook_zstd",
        build_file = "@psi//bazel:zstd.BUILD",
        strip_prefix = "zstd-1.5.5",
        sha256 = "98e9c3d949d1b924e28e01eccb7deed865eefebf25c2f21c702e5cd5b63b85e1",
        type = ".tar.gz",
        urls = [
            "https://github.com/facebook/zstd/archive/refs/tags/v1.5.5.tar.gz",
        ],
    )

def _upb():
    maybe(
        http_archive,
        name = "upb",
        sha256 = "017a7e8e4e842d01dba5dc8aa316323eee080cd1b75986a7d1f94d87220e6502",
        strip_prefix = "upb-e4635f223e7d36dfbea3b722a4ca4807a7e882e2",
        urls = [
            "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/upb/archive/e4635f223e7d36dfbea3b722a4ca4807a7e882e2.tar.gz",
            "https://github.com/protocolbuffers/upb/archive/e4635f223e7d36dfbea3b722a4ca4807a7e882e2.tar.gz",
        ],
    )

def _com_github_emptoolkit_emp_tool():
    maybe(
        http_archive,
        name = "com_github_emptoolkit_emp_tool",
        sha256 = "b9ab2380312e78020346b5d2db3d0244c7bd8098cb50f8b3620532ef491808d0",
        strip_prefix = "emp-tool-0.2.5",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = [
            "@psi//bazel:patches/emp-tool.patch",
            "@psi//bazel:patches/emp-tool-cmake.patch",
            "@psi//bazel:patches/emp-tool-sse2neon.patch",
        ],
        urls = [
            "https://github.com/emp-toolkit/emp-tool/archive/refs/tags/0.2.5.tar.gz",
        ],
        build_file = "@psi//bazel:emp-tool.BUILD",
    )

def _com_github_emptoolkit_emp_ot():
    maybe(
        http_archive,
        name = "com_github_emptoolkit_emp_ot",
        sha256 = "358036e5d18143720ee17103f8172447de23014bcfc1f8e7d5849c525ca928ac",
        strip_prefix = "emp-ot-0.2.4",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/emp-ot.patch"],
        urls = [
            "https://github.com/emp-toolkit/emp-ot/archive/refs/tags/0.2.4.tar.gz",
        ],
        build_file = "@psi//bazel:emp-ot.BUILD",
    )

def _com_github_intel_ipp():
    maybe(
        http_archive,
        name = "com_github_intel_ipp",
        sha256 = "1ecfa70328221748ceb694debffa0106b92e0f9bf6a484f8e8512c2730c7d730",
        strip_prefix = "ipp-crypto-ippcp_2021.8",
        build_file = "@psi//bazel:ipp.BUILD",
        patch_args = ["-p1"],
        patches = [
            "@psi//bazel:patches/ippcp.patch",
        ],
        urls = [
            "https://github.com/intel/ipp-crypto/archive/refs/tags/ippcp_2021.8.tar.gz",
        ],
    )

def _com_github_emptoolkit_emp_zk():
    maybe(
        http_archive,
        name = "com_github_emptoolkit_emp_zk",
        sha256 = "e02e6abc6ee14ca0e69e6f5f0efe24cab7da1bc905fc7c86a3e5a529114e489a",
        strip_prefix = "emp-zk-0.2.1",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/emp-zk.patch"],
        urls = [
            "https://github.com/emp-toolkit/emp-zk/archive/refs/tags/0.2.1.tar.gz",
        ],
        build_file = "@psi//bazel:emp-zk.BUILD",
    )

def _com_github_microsoft_seal():
    maybe(
        http_archive,
        name = "com_github_microsoft_seal",
        sha256 = "af9bf0f0daccda2a8b7f344f13a5692e0ee6a45fea88478b2b90c35648bf2672",
        strip_prefix = "SEAL-4.1.1",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/seal.patch"],
        urls = [
            "https://github.com/microsoft/SEAL/archive/refs/tags/v4.1.1.tar.gz",
        ],
        build_file = "@psi//bazel:seal.BUILD",
    )

def _com_github_eigenteam_eigen():
    maybe(
        http_archive,
        name = "com_github_eigenteam_eigen",
        sha256 = "c1b115c153c27c02112a0ecbf1661494295d9dcff6427632113f2e4af9f3174d",
        build_file = "@psi//bazel:eigen.BUILD",
        strip_prefix = "eigen-3.4",
        urls = [
            "https://gitlab.com/libeigen/eigen/-/archive/3.4/eigen-3.4.tar.gz",
        ],
    )

def _com_github_microsoft_apsi():
    maybe(
        http_archive,
        name = "com_github_microsoft_apsi",
        sha256 = "82c0f9329c79222675109d4a3682d204acd3ea9a724bcd98fa58eabe53851333",
        strip_prefix = "APSI-0.11.0",
        urls = [
            "https://github.com/microsoft/APSI/archive/refs/tags/v0.11.0.tar.gz",
        ],
        build_file = "@psi//bazel:microsoft_apsi.BUILD",
        patch_args = ["-p1"],
        patches = [
            "@psi//bazel:patches/apsi.patch",
            "@psi//bazel:patches/apsi-gen.patch",
            "@psi//bazel:patches/apsi_bin_bundle.patch",
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
        name = "com_github_microsoft_kuku",
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
        sha256 = "8aff985da30aaab37edf8e5b02fda33ed4cbdd962699a8e2af98fdef306f4e4d",
        strip_prefix = "flatbuffers-23.3.3",
        urls = [
            "https://github.com/google/flatbuffers/archive/refs/tags/v23.3.3.tar.gz",
        ],
    )

def _org_apache_arrow():
    maybe(
        http_archive,
        name = "org_apache_arrow",
        urls = [
            "https://github.com/apache/arrow/archive/apache-arrow-10.0.0.tar.gz",
        ],
        sha256 = "2852b21f93ee84185a9d838809c9a9c41bf6deca741bed1744e0fdba6cc19e3f",
        strip_prefix = "arrow-apache-arrow-10.0.0",
        build_file = "@psi//bazel:arrow.BUILD",
    )

def _com_github_grpc_grpc():
    maybe(
        http_archive,
        name = "com_github_grpc_grpc",
        sha256 = "7f42363711eb483a0501239fd5522467b31d8fe98d70d7867c6ca7b52440d828",
        strip_prefix = "grpc-1.51.0",
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/grpc.patch"],
        urls = [
            "https://github.com/grpc/grpc/archive/refs/tags/v1.51.0.tar.gz",
        ],
    )

def _com_github_nelhage_rules_boost():
    # use boost 1.83
    RULES_BOOST_COMMIT = "cfa585b1b5843993b70aa52707266dc23b3282d0"
    maybe(
        http_archive,
        name = "com_github_nelhage_rules_boost",
        sha256 = "a7c42df432fae9db0587ff778d84f9dc46519d67a984eff8c79ae35e45f277c1",
        strip_prefix = "rules_boost-%s" % RULES_BOOST_COMMIT,
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/boost.patch"],
        urls = [
            "https://github.com/nelhage/rules_boost/archive/%s.tar.gz" % RULES_BOOST_COMMIT,
        ],
    )

def _com_github_tencent_rapidjson():
    maybe(
        http_archive,
        name = "com_github_tencent_rapidjson",
        urls = [
            "https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz",
        ],
        sha256 = "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e",
        strip_prefix = "rapidjson-1.1.0",
        build_file = "@psi//bazel:rapidjson.BUILD",
    )

def _com_github_xtensor_xsimd():
    maybe(
        http_archive,
        name = "com_github_xtensor_xsimd",
        urls = [
            "https://codeload.github.com/xtensor-stack/xsimd/tar.gz/refs/tags/8.1.0",
        ],
        sha256 = "d52551360d37709675237d2a0418e28f70995b5b7cdad7c674626bcfbbf48328",
        type = "tar.gz",
        strip_prefix = "xsimd-8.1.0",
        build_file = "@psi//bazel:xsimd.BUILD",
    )

def _brotli():
    maybe(
        http_archive,
        name = "brotli",
        build_file = "@psi//bazel:brotli.BUILD",
        sha256 = "e720a6ca29428b803f4ad165371771f5398faba397edf6778837a18599ea13ff",
        strip_prefix = "brotli-1.1.0",
        urls = [
            "https://github.com/google/brotli/archive/refs/tags/v1.1.0.tar.gz",
        ],
    )

def _com_github_lz4_lz4():
    maybe(
        http_archive,
        name = "com_github_lz4_lz4",
        urls = [
            "https://codeload.github.com/lz4/lz4/tar.gz/refs/tags/v1.9.3",
        ],
        sha256 = "030644df4611007ff7dc962d981f390361e6c97a34e5cbc393ddfbe019ffe2c1",
        type = "tar.gz",
        strip_prefix = "lz4-1.9.3",
        build_file = "@psi//bazel:lz4.BUILD",
    )

def _org_apache_thrift():
    maybe(
        http_archive,
        name = "org_apache_thrift",
        build_file = "@psi//bazel:thrift.BUILD",
        sha256 = "6428911db505702c51f7d993155a4a4c8afee83fdd021b52f2eccd8d34780629",
        strip_prefix = "thrift-0.19.0",
        urls = [
            "https://github.com/apache/thrift/archive/v0.19.0.tar.gz",
        ],
    )

def _com_google_double_conversion():
    maybe(
        http_archive,
        name = "com_google_double_conversion",
        sha256 = "04ec44461850abbf33824da84978043b22554896b552c5fd11a9c5ae4b4d296e",
        strip_prefix = "double-conversion-3.3.0",
        build_file = "@psi//bazel:double-conversion.BUILD",
        urls = [
            "https://github.com/google/double-conversion/archive/refs/tags/v3.3.0.tar.gz",
        ],
    )

def _bzip2():
    maybe(
        http_archive,
        name = "bzip2",
        build_file = "@psi//bazel:bzip2.BUILD",
        sha256 = "ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269",
        strip_prefix = "bzip2-1.0.8",
        urls = [
            "https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz",
        ],
    )

def _com_github_google_snappy():
    maybe(
        http_archive,
        name = "com_github_google_snappy",
        urls = [
            "https://github.com/google/snappy/archive/refs/tags/1.1.9.tar.gz",
        ],
        sha256 = "75c1fbb3d618dd3a0483bff0e26d0a92b495bbe5059c8b4f1c962b478b6e06e7",
        strip_prefix = "snappy-1.1.9",
        build_file = "@psi//bazel:snappy.BUILD",
    )

def _com_github_google_perfetto():
    maybe(
        http_archive,
        name = "com_github_google_perfetto",
        urls = [
            "https://github.com/google/perfetto/archive/refs/tags/v41.0.tar.gz",
        ],
        sha256 = "4c8fe8a609fcc77ca653ec85f387ab6c3a048fcd8df9275a1aa8087984b89db8",
        strip_prefix = "perfetto-41.0",
        patch_args = ["-p1"],
        patches = ["@psi//bazel:patches/perfetto.patch"],
        build_file = "@psi//bazel:perfetto.BUILD",
    )

def _com_github_floodyberry_curve25519_donna():
    maybe(
        http_archive,
        name = "com_github_floodyberry_curve25519_donna",
        strip_prefix = "curve25519-donna-2fe66b65ea1acb788024f40a3373b8b3e6f4bbb2",
        sha256 = "ba57d538c241ad30ff85f49102ab2c8dd996148456ed238a8c319f263b7b149a",
        type = "tar.gz",
        build_file = "@psi//bazel:curve25519-donna.BUILD",
        urls = [
            "https://github.com/floodyberry/curve25519-donna/archive/2fe66b65ea1acb788024f40a3373b8b3e6f4bbb2.tar.gz",
        ],
    )

def _com_github_ridiculousfish_libdivide():
    maybe(
        http_archive,
        name = "com_github_ridiculousfish_libdivide",
        urls = [
            "https://github.com/ridiculousfish/libdivide/archive/refs/tags/5.0.tar.gz",
        ],
        sha256 = "01ffdf90bc475e42170741d381eb9cfb631d9d7ddac7337368bcd80df8c98356",
        strip_prefix = "libdivide-5.0",
        build_file = "@psi//bazel:libdivide.BUILD",
    )

def _com_github_sparsehash_sparsehash():
    maybe(
        http_archive,
        name = "com_github_sparsehash_sparsehash",
        urls = [
            "https://github.com/sparsehash/sparsehash/archive/refs/tags/sparsehash-2.0.4.tar.gz",
        ],
        sha256 = "8cd1a95827dfd8270927894eb77f62b4087735cbede953884647f16c521c7e58",
        strip_prefix = "sparsehash-sparsehash-2.0.4",
        build_file = "@psi//bazel:sparsehash.BUILD",
    )
