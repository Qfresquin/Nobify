#include "nob_codegen_internal.h"

static const char *cg_platform_name(Nob_Codegen_Platform platform) {
    switch (platform) {
        case NOB_CODEGEN_PLATFORM_HOST: return "host";
        case NOB_CODEGEN_PLATFORM_LINUX: return "linux";
        case NOB_CODEGEN_PLATFORM_DARWIN: return "darwin";
        case NOB_CODEGEN_PLATFORM_WINDOWS: return "windows";
    }
    return "unknown";
}

static const char *cg_backend_name(Nob_Codegen_Backend backend) {
    switch (backend) {
        case NOB_CODEGEN_BACKEND_AUTO: return "auto";
        case NOB_CODEGEN_BACKEND_POSIX: return "posix";
        case NOB_CODEGEN_BACKEND_WIN32_MSVC: return "win32-msvc";
    }
    return "unknown";
}

static Nob_Codegen_Platform cg_host_platform(void) {
#if defined(_WIN32)
    return NOB_CODEGEN_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return NOB_CODEGEN_PLATFORM_DARWIN;
#else
    return NOB_CODEGEN_PLATFORM_LINUX;
#endif
}

static CG_Artifact_Naming cg_artifact_naming(const char *prefix, const char *suffix) {
    CG_Artifact_Naming naming = {0};
    naming.prefix = nob_sv_from_cstr(prefix ? prefix : "");
    naming.suffix = nob_sv_from_cstr(suffix ? suffix : "");
    return naming;
}

static bool cg_resolve_platform_backend_pair(Nob_Codegen_Platform requested_platform,
                                             Nob_Codegen_Backend requested_backend,
                                             Nob_Codegen_Platform *out_platform,
                                             Nob_Codegen_Backend *out_backend) {
    Nob_Codegen_Platform platform = requested_platform;
    Nob_Codegen_Backend backend = requested_backend;
    if (out_platform) *out_platform = NOB_CODEGEN_PLATFORM_HOST;
    if (out_backend) *out_backend = NOB_CODEGEN_BACKEND_AUTO;

    if (platform == NOB_CODEGEN_PLATFORM_HOST) {
        platform = cg_host_platform();
    }
    if (backend == NOB_CODEGEN_BACKEND_AUTO) {
        switch (platform) {
            case NOB_CODEGEN_PLATFORM_LINUX:
            case NOB_CODEGEN_PLATFORM_DARWIN:
                backend = NOB_CODEGEN_BACKEND_POSIX;
                break;

            case NOB_CODEGEN_PLATFORM_WINDOWS:
                backend = NOB_CODEGEN_BACKEND_WIN32_MSVC;
                break;

            case NOB_CODEGEN_PLATFORM_HOST:
                backend = NOB_CODEGEN_BACKEND_AUTO;
                break;
        }
    }

    if ((platform == NOB_CODEGEN_PLATFORM_LINUX || platform == NOB_CODEGEN_PLATFORM_DARWIN) &&
        backend != NOB_CODEGEN_BACKEND_POSIX) {
        nob_log(NOB_ERROR,
                "codegen: invalid platform/backend pair: %s + %s",
                cg_platform_name(platform),
                cg_backend_name(backend));
        return false;
    }
    if (platform == NOB_CODEGEN_PLATFORM_WINDOWS &&
        backend != NOB_CODEGEN_BACKEND_WIN32_MSVC) {
        nob_log(NOB_ERROR,
                "codegen: invalid platform/backend pair: %s + %s",
                cg_platform_name(platform),
                cg_backend_name(backend));
        return false;
    }
    if (platform == NOB_CODEGEN_PLATFORM_HOST || backend == NOB_CODEGEN_BACKEND_AUTO) {
        nob_log(NOB_ERROR, "codegen: failed to resolve generation platform/backend");
        return false;
    }

    if (out_platform) *out_platform = platform;
    if (out_backend) *out_backend = backend;
    return true;
}

static bool cg_policy_is_windows(const CG_Context *ctx) {
    return ctx && ctx->policy.platform == NOB_CODEGEN_PLATFORM_WINDOWS;
}

static bool cg_init_backend_policy(CG_Context *ctx) {
    Nob_Codegen_Platform platform = NOB_CODEGEN_PLATFORM_HOST;
    Nob_Codegen_Backend backend = NOB_CODEGEN_BACKEND_AUTO;
    if (!ctx) return false;
    if (!cg_resolve_platform_backend_pair(ctx->opts.target_platform,
                                          ctx->opts.backend,
                                          &platform,
                                          &backend)) {
        return false;
    }

    ctx->policy = (CG_Backend_Policy){
        .platform = platform,
        .backend = backend,
        .execution_supported = platform == NOB_CODEGEN_PLATFORM_LINUX &&
                               backend == NOB_CODEGEN_BACKEND_POSIX,
    };

    switch (platform) {
        case NOB_CODEGEN_PLATFORM_LINUX:
            ctx->policy.platform_id = nob_sv_from_cstr("Linux");
            ctx->policy.executable = cg_artifact_naming("", "");
            ctx->policy.static_library = cg_artifact_naming("lib", ".a");
            ctx->policy.shared_runtime = cg_artifact_naming("lib", ".so");
            ctx->policy.shared_linker = cg_artifact_naming("lib", ".so");
            ctx->policy.module_runtime = cg_artifact_naming("lib", ".so");
            ctx->policy.module_linker = cg_artifact_naming("lib", ".so");
            ctx->policy.object_suffix = nob_sv_from_cstr(".o");
            ctx->policy.c_compiler_default = nob_sv_from_cstr("cc");
            ctx->policy.cxx_compiler_default = nob_sv_from_cstr("c++");
            ctx->policy.archive_tool_default = nob_sv_from_cstr("ar");
            ctx->policy.link_tool_default = nob_sv_from_cstr("");
            ctx->policy.shared_link_flag = nob_sv_from_cstr("-shared");
            ctx->policy.module_link_flag = nob_sv_from_cstr("-shared");
            ctx->policy.use_compiler_driver_for_executable_link = true;
            ctx->policy.use_compiler_driver_for_shared_link = true;
            ctx->policy.use_compiler_driver_for_module_link = true;
            break;

        case NOB_CODEGEN_PLATFORM_DARWIN:
            ctx->policy.platform_id = nob_sv_from_cstr("Darwin");
            ctx->policy.executable = cg_artifact_naming("", "");
            ctx->policy.static_library = cg_artifact_naming("lib", ".a");
            ctx->policy.shared_runtime = cg_artifact_naming("lib", ".dylib");
            ctx->policy.shared_linker = cg_artifact_naming("lib", ".dylib");
            ctx->policy.module_runtime = cg_artifact_naming("lib", ".so");
            ctx->policy.module_linker = cg_artifact_naming("lib", ".so");
            ctx->policy.object_suffix = nob_sv_from_cstr(".o");
            ctx->policy.c_compiler_default = nob_sv_from_cstr("cc");
            ctx->policy.cxx_compiler_default = nob_sv_from_cstr("c++");
            ctx->policy.archive_tool_default = nob_sv_from_cstr("ar");
            ctx->policy.link_tool_default = nob_sv_from_cstr("");
            ctx->policy.shared_link_flag = nob_sv_from_cstr("-dynamiclib");
            ctx->policy.module_link_flag = nob_sv_from_cstr("-bundle");
            ctx->policy.use_compiler_driver_for_executable_link = true;
            ctx->policy.use_compiler_driver_for_shared_link = true;
            ctx->policy.use_compiler_driver_for_module_link = true;
            break;

        case NOB_CODEGEN_PLATFORM_WINDOWS:
            ctx->policy.platform_id = nob_sv_from_cstr("Windows");
            ctx->policy.executable = cg_artifact_naming("", ".exe");
            ctx->policy.static_library = cg_artifact_naming("", ".lib");
            ctx->policy.shared_runtime = cg_artifact_naming("", ".dll");
            ctx->policy.shared_linker = cg_artifact_naming("", ".lib");
            ctx->policy.module_runtime = cg_artifact_naming("", ".dll");
            ctx->policy.module_linker = cg_artifact_naming("", ".lib");
            ctx->policy.object_suffix = nob_sv_from_cstr(".obj");
            ctx->policy.c_compiler_default = nob_sv_from_cstr("cl.exe");
            ctx->policy.cxx_compiler_default = nob_sv_from_cstr("cl.exe");
            ctx->policy.archive_tool_default = nob_sv_from_cstr("lib.exe");
            ctx->policy.link_tool_default = nob_sv_from_cstr("link.exe");
            ctx->policy.shared_link_flag = nob_sv_from_cstr("/DLL");
            ctx->policy.module_link_flag = nob_sv_from_cstr("/DLL");
            ctx->policy.use_compiler_driver_for_executable_link = false;
            ctx->policy.use_compiler_driver_for_shared_link = false;
            ctx->policy.use_compiler_driver_for_module_link = false;
            ctx->policy.shared_has_distinct_linker_artifact = true;
            ctx->policy.module_has_distinct_linker_artifact = true;
            break;

        case NOB_CODEGEN_PLATFORM_HOST:
            return false;
    }

    return true;
}
