load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_import(
    name = "hexl_lib",
    static_library = "lib/libhexl.a",
)

cc_library(
    name = "hexl",
    hdrs = glob(["include/hexl/**/*.h", "include/hexl/**/*.hpp"]),
    includes = ["include"],
    deps = [":hexl_lib"],
)
