_HEXL_BUILD = """load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

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
"""

def _hexl_repository_impl(repository_ctx):
    roots = []
    env_root = repository_ctx.os.environ.get("HEXL_ROOT", "")
    if env_root:
        roots.append(env_root)

    roots.extend([
        "/usr/local",
        "/usr",
    ])

    for root in roots:
        include_dir = repository_ctx.path(root + "/include")
        header_dir = repository_ctx.path(root + "/include/hexl")
        for lib_dir_name in ["lib", "lib64"]:
            lib_dir = repository_ctx.path(root + "/" + lib_dir_name)
            static_lib = repository_ctx.path(root + "/" + lib_dir_name + "/libhexl.a")
            if header_dir.exists and static_lib.exists:
                repository_ctx.symlink(include_dir, "include")
                repository_ctx.symlink(lib_dir, "lib")
                repository_ctx.file("BUILD.bazel", _HEXL_BUILD)
                repository_ctx.file("WORKSPACE.bazel", "workspace(name = \"hexl\")\n")
                return

    fail(
        "HEXL was not found. Set HEXL_ROOT to the installation prefix, "
        + "or install HEXL under /usr/local or /usr."
    )

hexl_repository = repository_rule(
    implementation = _hexl_repository_impl,
    environ = ["HEXL_ROOT"],
    local = True,
)
