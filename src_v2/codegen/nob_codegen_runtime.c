#include "nob_codegen_internal.h"

static bool cg_step_uses_bare_tool(CG_Context *ctx, const char *tool_name) {
    if (!ctx || !tool_name) return false;
    for (size_t step_index = 0; step_index < bm_query_build_step_count(ctx->model); ++step_index) {
        BM_Build_Step_Id id = (BM_Build_Step_Id)step_index;
        for (size_t cmd_index = 0; cmd_index < bm_query_build_step_command_count(ctx->model, id); ++cmd_index) {
            BM_String_Span argv = bm_query_build_step_command_argv(ctx->model, id, cmd_index);
            if (argv.count == 0) continue;
            if (nob_sv_eq(argv.items[0], nob_sv_from_cstr(tool_name))) return true;
        }
    }
    return false;
}

void cg_collect_helper_requirements(CG_Context *ctx) {
    bool needs_compile_toolchain = false;
    bool needs_archive_tool = false;
    bool needs_link_tool = false;
    bool needs_install_copy_file = false;
    bool needs_install_copy_directory = false;
    bool needs_require_paths = false;
    if (!ctx) return;

    /* clean_all() always owns backend-private paths, so filesystem helpers are always needed */
    ctx->helper_bits |= CG_HELPER_FILESYSTEM;

    if (arena_arr_len(ctx->known_configs) > 0) ctx->helper_bits |= CG_HELPER_CONFIG_MATCHES;

    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (ctx->targets[i].alias || ctx->targets[i].imported) continue;
        if (ctx->targets[i].emits_artifact) {
            needs_compile_toolchain = true;
            needs_require_paths = true;
            if (ctx->targets[i].kind == BM_TARGET_STATIC_LIBRARY) {
                needs_archive_tool = true;
            } else if (ctx->targets[i].kind == BM_TARGET_EXECUTABLE ||
                       ctx->targets[i].kind == BM_TARGET_SHARED_LIBRARY ||
                       ctx->targets[i].kind == BM_TARGET_MODULE_LIBRARY) {
                if (!ctx->policy.use_compiler_driver_for_executable_link ||
                    !ctx->policy.use_compiler_driver_for_shared_link ||
                    !ctx->policy.use_compiler_driver_for_module_link) {
                    needs_link_tool = true;
                }
            }
        }
    }

    if (ctx->build_step_count > 0) {
        ctx->helper_bits |= CG_HELPER_RUN_CMD;
        needs_require_paths = true;
    }
    if (cg_step_uses_bare_tool(ctx, "cmake")) ctx->helper_bits |= CG_HELPER_CMAKE_RESOLVER;
    if (cg_step_uses_bare_tool(ctx, "cpack")) ctx->helper_bits |= CG_HELPER_CPACK_RESOLVER;

    for (size_t rule_index = 0; rule_index < bm_query_install_rule_count(ctx->model); ++rule_index) {
        BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, (BM_Install_Rule_Id)rule_index);
        if (kind == BM_INSTALL_RULE_DIRECTORY) {
            needs_install_copy_directory = true;
        } else {
            needs_install_copy_file = true;
        }
    }

    if (needs_compile_toolchain) ctx->helper_bits |= CG_HELPER_COMPILE_TOOLCHAIN;
    if (needs_archive_tool) ctx->helper_bits |= CG_HELPER_ARCHIVE_TOOL;
    if (needs_link_tool) ctx->helper_bits |= CG_HELPER_LINK_TOOL;
    if (needs_require_paths) ctx->helper_bits |= CG_HELPER_STAMP;
    if (needs_install_copy_file) ctx->helper_bits |= CG_HELPER_INSTALL_COPY_FILE;
    if (needs_install_copy_directory) ctx->helper_bits |= CG_HELPER_INSTALL_COPY_DIRECTORY;
}

bool cg_emit_support_helpers(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    nob_sb_append_cstr(out, "static const char *g_build_config = \"\";\n\n");

    if (ctx->helper_bits & CG_HELPER_CONFIG_MATCHES) {
        nob_sb_append_cstr(out,
            "static bool config_matches(const char *actual, const char *expected) {\n"
            "    if (!expected) return false;\n"
            "    if (!actual) actual = \"\";\n"
            "    while (*actual && *expected) {\n"
            "        if (tolower((unsigned char)*actual) != tolower((unsigned char)*expected)) return false;\n"
            "        ++actual;\n"
            "        ++expected;\n"
            "    }\n"
            "    return *actual == '\\0' && *expected == '\\0';\n"
            "}\n\n");
    }

    if (ctx->helper_bits & (CG_HELPER_COMPILE_TOOLCHAIN |
                            CG_HELPER_ARCHIVE_TOOL |
                            CG_HELPER_LINK_TOOL)) {
        nob_sb_append_cstr(out,
            "static const char *resolve_cc_bin(void) {\n"
            "    const char *tool = getenv(\"CC\");\n"
            "    if (tool && tool[0] != '\\0') return tool;\n"
            "    return ");
        if (!cg_sb_append_c_string(out, ctx->policy.c_compiler_default)) return false;
        nob_sb_append_cstr(out,
            ";\n"
            "}\n\n"
            "static const char *resolve_cxx_bin(void) {\n"
            "    const char *tool = getenv(\"CXX\");\n"
            "    if (tool && tool[0] != '\\0') return tool;\n"
            "    return ");
        if (!cg_sb_append_c_string(out, ctx->policy.cxx_compiler_default)) return false;
        nob_sb_append_cstr(out,
            ";\n"
            "}\n\n"
            "static void append_toolchain_cmd(Nob_Cmd *cmd, bool use_cxx) {\n"
            "    nob_cmd_append(cmd, use_cxx ? resolve_cxx_bin() : resolve_cc_bin());\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_ARCHIVE_TOOL) {
        nob_sb_append_cstr(out,
            "static const char *resolve_archive_bin(void) {\n"
            "    return ");
        if (!cg_sb_append_c_string(out, ctx->policy.archive_tool_default)) return false;
        nob_sb_append_cstr(out,
            ";\n"
            "}\n\n"
            "static void append_archive_tool_cmd(Nob_Cmd *cmd) {\n"
            "    nob_cmd_append(cmd, resolve_archive_bin());\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_LINK_TOOL) {
        nob_sb_append_cstr(out,
            "static const char *resolve_link_bin(void) {\n"
            "    return ");
        if (!cg_sb_append_c_string(out, ctx->policy.link_tool_default)) return false;
        nob_sb_append_cstr(out,
            ";\n"
            "}\n\n"
            "static void append_link_tool_cmd(Nob_Cmd *cmd, bool use_cxx) {\n");
        if (ctx->policy.use_compiler_driver_for_executable_link ||
            ctx->policy.use_compiler_driver_for_shared_link ||
            ctx->policy.use_compiler_driver_for_module_link) {
            nob_sb_append_cstr(out, "    append_toolchain_cmd(cmd, use_cxx);\n");
        } else {
            nob_sb_append_cstr(out,
                "    (void)use_cxx;\n"
                "    nob_cmd_append(cmd, resolve_link_bin());\n");
        }
        nob_sb_append_cstr(out, "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_CMAKE_RESOLVER) {
        if (!cg_emit_preferred_tool_resolver(out,
                                             "resolve_cmake_bin",
                                             "NOB_CMAKE_BIN",
                                             ctx->embedded_cmake_bin_abs,
                                             "cmake")) {
            return false;
        }
    }
    if (ctx->helper_bits & CG_HELPER_CPACK_RESOLVER) {
        if (!cg_emit_preferred_tool_resolver(out,
                                             "resolve_cpack_bin",
                                             "NOB_CPACK_BIN",
                                             ctx->embedded_cpack_bin_abs,
                                             "cpack")) {
            return false;
        }
    }

    if (ctx->helper_bits & CG_HELPER_FILESYSTEM) {
        nob_sb_append_cstr(out,
            "static bool ensure_dir(const char *path) {\n"
            "    char buf[4096] = {0};\n"
            "    size_t len = 0;\n"
            "    size_t start = 1;\n"
            "    if (!path || path[0] == '\\0' || strcmp(path, \".\") == 0) return true;\n"
            "    len = strlen(path);\n"
            "    if (len >= sizeof(buf)) return false;\n"
            "    memcpy(buf, path, len + 1);\n"
            "    if (len >= 3 && buf[1] == ':' && (buf[2] == '/' || buf[2] == '\\\\')) start = 3;\n"
            "    for (size_t i = start; i < len; ++i) {\n"
            "        if (buf[i] != '/' && buf[i] != '\\\\') continue;\n"
            "        buf[i] = '\\0';\n"
            "        if (buf[0] != '\\0' && !nob_mkdir_if_not_exists(buf)) return false;\n"
            "        buf[i] = '/';\n"
            "    }\n"
            "    return nob_mkdir_if_not_exists(buf);\n"
            "}\n\n"
            "static bool ensure_parent_dir(const char *path) {\n"
            "    const char *dir = nob_temp_dir_name(path);\n"
            "    if (!dir || strcmp(dir, \".\") == 0) return true;\n"
            "    return ensure_dir(dir);\n"
            "}\n\n"
            "static bool remove_path_recursive_entry(Nob_Walk_Entry entry) {\n"
            "    return nob_delete_file(entry.path);\n"
            "}\n\n"
            "static bool remove_path_recursive(const char *path) {\n"
            "    if (!path || path[0] == '\\0') return true;\n"
            "    if (!nob_file_exists(path)) return true;\n"
            "    return nob_walk_dir(path, remove_path_recursive_entry, .post_order = true);\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_RUN_CMD) {
        nob_sb_append_cstr(out,
            "static bool run_cmd_in_dir(const char *working_dir, Nob_Cmd *cmd) {\n"
            "    const char *saved_dir = NULL;\n"
            "    bool ok = false;\n"
            "    if (working_dir && working_dir[0] != '\\0') {\n"
            "        saved_dir = nob_get_current_dir_temp();\n"
            "        if (!saved_dir) return false;\n"
            "        saved_dir = nob_temp_strdup(saved_dir);\n"
            "        if (!saved_dir) return false;\n"
            "        if (!nob_set_current_dir(working_dir)) return false;\n"
            "    }\n"
            "    ok = nob_cmd_run(cmd);\n"
            "    if (working_dir && working_dir[0] != '\\0') {\n"
            "        if (!nob_set_current_dir(saved_dir)) return false;\n"
            "    }\n"
            "    return ok;\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_STAMP) {
        nob_sb_append_cstr(out,
            "static bool write_stamp(const char *path) {\n"
            "    if (!ensure_parent_dir(path)) return false;\n"
            "    return nob_write_entire_file(path, \"\", 0);\n"
            "}\n\n"
            "static bool require_paths(const char *const *paths, size_t count) {\n"
            "    for (size_t i = 0; i < count; ++i) {\n"
            "        if (nob_file_exists(paths[i])) continue;\n"
            "        nob_log(NOB_ERROR, \"codegen: declared build output is missing: %s\", paths[i]);\n"
            "        return false;\n"
            "    }\n"
            "    return true;\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_INSTALL_COPY_FILE) {
        nob_sb_append_cstr(out,
            "static bool install_copy_file(const char *src_path, const char *dst_path) {\n"
            "    if (!ensure_parent_dir(dst_path)) return false;\n"
            "    return nob_copy_file(src_path, dst_path);\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_INSTALL_COPY_DIRECTORY) {
        nob_sb_append_cstr(out,
            "static bool install_copy_directory(const char *src_path, const char *dst_path) {\n"
            "    if (!ensure_parent_dir(dst_path)) return false;\n"
            "    return nob_copy_directory_recursively(src_path, dst_path);\n"
            "}\n\n");
    }

    return true;
}
