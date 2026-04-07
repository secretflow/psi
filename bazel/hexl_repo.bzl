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

_HEXL_STUB_BUILD = """load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "hexl",
)
"""

def _hexl_repository_impl(repository_ctx):
    roots = []
    env_root = repository_ctx.os.environ.get("HEXL_ROOT", "")
    if env_root:
        roots.append(env_root)

    roots.extend([
        "/usr/local/opt/intel-hexl",
        "/usr/local",
        "/opt/homebrew/opt/intel-hexl",
        "/opt/homebrew",
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

    # HEXL not found — create a stub so the workspace resolves on non-x86
    # platforms.  Targets that actually depend on @hexl are gated by
    # target_compatible_with = ["@platforms//cpu:x86_64"] and will never be
    # built on ARM / macOS Apple Silicon.
    repository_ctx.file("BUILD.bazel", _HEXL_STUB_BUILD)
    repository_ctx.file("WORKSPACE.bazel", "workspace(name = \"hexl\")\n")

hexl_repository = repository_rule(
    implementation = _hexl_repository_impl,
    environ = ["HEXL_ROOT"],
    local = True,
)
