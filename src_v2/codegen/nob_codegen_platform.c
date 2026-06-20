#include "nob_codegen_internal.h"

static const char *cg_platform_name(Nob_Codegen_Platform platform) {
    switch (platform) {
        case NOB_CODEGEN_PLATFORM_HOST: return "host";
        case NOB_CODEGEN_PLATFORM_LINUX: return "linux";
        case NOB_CODEGEN_PLATFORM_DARWIN: return "darwin";
        case NOB_CODEGEN_PLATFORM_WINDOWS: return "windows";
        case NOB_CODEGEN_PLATFORM_ANDROID: return "android";
        case NOB_CODEGEN_PLATFORM_IOS: return "ios";
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
            case NOB_CODEGEN_PLATFORM_ANDROID:
            case NOB_CODEGEN_PLATFORM_IOS:
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

    if ((platform == NOB_CODEGEN_PLATFORM_LINUX ||
         platform == NOB_CODEGEN_PLATFORM_DARWIN ||
         platform == NOB_CODEGEN_PLATFORM_ANDROID ||
         platform == NOB_CODEGEN_PLATFORM_IOS) &&
        backend != NOB_CODEGEN_BACKEND_POSIX) {
        nob_log(NOB_ERROR,
                "codegen: invalid platform/backend pair: %s + %s",
                cg_platform_name(platform),
                cg_backend_name(backend));
        return false;
    }
    if (platform == NOB_CODEGEN_PLATFORM_WINDOWS &&
        backend != NOB_CODEGEN_BACKEND_WIN32_MSVC &&
        backend != NOB_CODEGEN_BACKEND_POSIX) {
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

static bool cg_policy_uses_msvc_command_line(const CG_Context *ctx) {
    return ctx && ctx->policy.backend == NOB_CODEGEN_BACKEND_WIN32_MSVC;
}

static Nob_Codegen_Platform cg_platform_from_toolchain_system(String_View system_name) {
    if (nob_sv_eq(system_name, nob_sv_from_cstr("Windows"))) return NOB_CODEGEN_PLATFORM_WINDOWS;
    if (nob_sv_eq(system_name, nob_sv_from_cstr("Android"))) return NOB_CODEGEN_PLATFORM_ANDROID;
    if (nob_sv_eq(system_name, nob_sv_from_cstr("iOS"))) return NOB_CODEGEN_PLATFORM_IOS;
    if (nob_sv_eq(system_name, nob_sv_from_cstr("Darwin"))) return NOB_CODEGEN_PLATFORM_DARWIN;
    if (nob_sv_eq(system_name, nob_sv_from_cstr("Linux"))) return NOB_CODEGEN_PLATFORM_LINUX;
    return NOB_CODEGEN_PLATFORM_HOST;
}

static void cg_apply_toolchain_snapshot_to_opts(CG_Context *ctx) {
    if (!ctx || !ctx->toolchain) return;
    const Event_Toolchain_Snapshot *tc = ctx->toolchain;
    Nob_Codegen_Platform requested_platform = ctx->opts.target_platform;
    Nob_Codegen_Backend requested_backend = ctx->opts.backend;
    Nob_Codegen_Platform snapshot_platform = tc->platform_id.count > 0
        ? cg_platform_from_toolchain_system(tc->platform_id)
        : cg_platform_from_toolchain_system(tc->target_system_name);
    bool snapshot_compatible = false;
    if (ctx->opts.target_platform == NOB_CODEGEN_PLATFORM_HOST) {
        if (snapshot_platform != NOB_CODEGEN_PLATFORM_HOST) ctx->opts.target_platform = snapshot_platform;
        snapshot_compatible = true;
    } else if (snapshot_platform == requested_platform) {
        if (requested_backend == NOB_CODEGEN_BACKEND_AUTO) {
            snapshot_compatible = true;
        } else if (requested_platform == NOB_CODEGEN_PLATFORM_WINDOWS) {
            snapshot_compatible = (tc->msvc && requested_backend == NOB_CODEGEN_BACKEND_WIN32_MSVC) ||
                                  (!tc->msvc && requested_backend == NOB_CODEGEN_BACKEND_POSIX);
        } else {
            snapshot_compatible = requested_backend == NOB_CODEGEN_BACKEND_POSIX;
        }
    }
    if (ctx->opts.backend == NOB_CODEGEN_BACKEND_AUTO && ctx->opts.target_platform == NOB_CODEGEN_PLATFORM_WINDOWS) {
        ctx->opts.backend = tc->msvc ? NOB_CODEGEN_BACKEND_WIN32_MSVC : NOB_CODEGEN_BACKEND_POSIX;
    }
    if (!snapshot_compatible) return;
    if (ctx->opts.c_compiler.count == 0) ctx->opts.c_compiler = tc->c.compiler;
    if (ctx->opts.cxx_compiler.count == 0) ctx->opts.cxx_compiler = tc->cxx.compiler;
    if (ctx->opts.archive_tool.count == 0) ctx->opts.archive_tool = tc->archive_tool;
    if (ctx->opts.link_tool.count == 0) ctx->opts.link_tool = tc->link_tool;
}

static void cg_apply_toolchain_snapshot_to_policy(CG_Context *ctx) {
    if (!ctx || !ctx->toolchain) return;
    const Event_Toolchain_Snapshot *tc = ctx->toolchain;
    Nob_Codegen_Platform snapshot_platform = tc->platform_id.count > 0
        ? cg_platform_from_toolchain_system(tc->platform_id)
        : cg_platform_from_toolchain_system(tc->target_system_name);
    if (snapshot_platform != ctx->policy.platform) return;
    if (ctx->policy.platform == NOB_CODEGEN_PLATFORM_WINDOWS) {
        bool compatible_backend = (tc->msvc && ctx->policy.backend == NOB_CODEGEN_BACKEND_WIN32_MSVC) ||
                                  (!tc->msvc && ctx->policy.backend == NOB_CODEGEN_BACKEND_POSIX);
        if (!compatible_backend) return;
    }
    if (tc->object_suffix.count > 0) ctx->policy.object_suffix = tc->object_suffix;
    if (tc->executable_suffix.count > 0) ctx->policy.executable.suffix = tc->executable_suffix;
    if (tc->static_library_prefix.count > 0 || tc->static_library_suffix.count > 0) {
        ctx->policy.static_library.prefix = tc->static_library_prefix;
        ctx->policy.static_library.suffix = tc->static_library_suffix;
    }
    if (tc->shared_library_prefix.count > 0 || tc->shared_library_suffix.count > 0) {
        ctx->policy.shared_runtime.prefix = tc->shared_library_prefix;
        ctx->policy.shared_runtime.suffix = tc->shared_library_suffix;
    }
    if (tc->shared_linker_prefix.count > 0 || tc->shared_linker_suffix.count > 0) {
        ctx->policy.shared_linker.prefix = tc->shared_linker_prefix;
        ctx->policy.shared_linker.suffix = tc->shared_linker_suffix;
    }
    if (tc->module_library_prefix.count > 0 || tc->module_library_suffix.count > 0) {
        ctx->policy.module_runtime.prefix = tc->module_library_prefix;
        ctx->policy.module_runtime.suffix = tc->module_library_suffix;
    }
    if (tc->module_linker_prefix.count > 0 || tc->module_linker_suffix.count > 0) {
        ctx->policy.module_linker.prefix = tc->module_linker_prefix;
        ctx->policy.module_linker.suffix = tc->module_linker_suffix;
    }
    bool has_platform_rules = tc->platform_id.count > 0 ||
                              tc->shared_link_flag.count > 0 ||
                              tc->module_link_flag.count > 0 ||
                              tc->shared_linker_prefix.count > 0 ||
                              tc->shared_linker_suffix.count > 0 ||
                              tc->module_linker_prefix.count > 0 ||
                              tc->module_linker_suffix.count > 0;
    if (tc->shared_link_flag.count > 0) ctx->policy.shared_link_flag = tc->shared_link_flag;
    if (tc->module_link_flag.count > 0) ctx->policy.module_link_flag = tc->module_link_flag;
    if (has_platform_rules) {
        ctx->policy.shared_has_distinct_linker_artifact = tc->shared_has_distinct_linker_artifact;
        ctx->policy.module_has_distinct_linker_artifact = tc->module_has_distinct_linker_artifact;
        ctx->policy.use_compiler_driver_for_shared_link = tc->shared_uses_compiler_driver;
        ctx->policy.use_compiler_driver_for_module_link = tc->module_uses_compiler_driver;
    }
    ctx->policy.sdkroot = tc->sdkroot;
    ctx->policy.osx_architectures = tc->osx_architectures;
    ctx->policy.osx_deployment_target = tc->osx_deployment_target;
    ctx->policy.android_abi = tc->android_abi;
    ctx->policy.android_api = tc->android_api;
    ctx->policy.android_ndk = tc->android_ndk;
    if (tc->c.compiler.count > 0) ctx->policy.c_compiler_default = tc->c.compiler;
    if (tc->cxx.compiler.count > 0) ctx->policy.cxx_compiler_default = tc->cxx.compiler;
    if (tc->archive_tool.count > 0) ctx->policy.archive_tool_default = tc->archive_tool;
    if (tc->link_tool.count > 0) ctx->policy.link_tool_default = tc->link_tool;
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
    if (platform == NOB_CODEGEN_PLATFORM_WINDOWS &&
        backend == NOB_CODEGEN_BACKEND_POSIX &&
        !(ctx->toolchain && ctx->toolchain->target_windows && !ctx->toolchain->msvc)) {
        nob_log(NOB_ERROR,
                "codegen: invalid platform/backend pair: %s + %s",
                cg_platform_name(platform),
                cg_backend_name(backend));
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

        case NOB_CODEGEN_PLATFORM_ANDROID:
            ctx->policy.platform_id = nob_sv_from_cstr("Android");
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

        case NOB_CODEGEN_PLATFORM_IOS:
            ctx->policy.platform_id = nob_sv_from_cstr("iOS");
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
            ctx->policy.shared_runtime = cg_artifact_naming("", ".dll");
            ctx->policy.module_runtime = cg_artifact_naming("", ".dll");
            if (backend == NOB_CODEGEN_BACKEND_POSIX) {
                ctx->policy.static_library = cg_artifact_naming("lib", ".a");
                ctx->policy.shared_linker = cg_artifact_naming("lib", ".dll");
                ctx->policy.module_linker = cg_artifact_naming("lib", ".dll");
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
            } else {
                ctx->policy.static_library = cg_artifact_naming("", ".lib");
                ctx->policy.shared_linker = cg_artifact_naming("", ".lib");
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
            }
            break;

        case NOB_CODEGEN_PLATFORM_HOST:
            return false;
    }

    cg_apply_toolchain_snapshot_to_policy(ctx);
    return true;
}
