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

static bool cg_package_generator_enabled(CG_Context *ctx, const char *generator_name) {
    if (!ctx || !generator_name) return false;
    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_String_Span generators =
            bm_query_cpack_package_generators(ctx->model, (BM_CPack_Package_Id)package_index);
        for (size_t i = 0; i < generators.count; ++i) {
            if (nob_sv_eq(generators.items[i], nob_sv_from_cstr(generator_name))) return true;
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
    bool needs_write_stamp = false;
    bool needs_package_archive = false;
    bool needs_tar_resolver = false;
    bool needs_replay_sha256 = false;
    if (!ctx) return;

    /* clean_all() always owns backend-private paths, so filesystem helpers are always needed */
    ctx->helper_bits |= CG_HELPER_FILESYSTEM;
    ctx->helper_bits |= CG_HELPER_RUN_CMD;

    if (arena_arr_len(ctx->known_configs) > 0) ctx->helper_bits |= CG_HELPER_CONFIG_MATCHES;

    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (ctx->targets[i].alias || ctx->targets[i].imported) continue;
        if (ctx->targets[i].emits_artifact) {
            needs_compile_toolchain = true;
            if (ctx->targets[i].kind == BM_TARGET_STATIC_LIBRARY) {
                needs_archive_tool = true;
            } else if (ctx->targets[i].kind == BM_TARGET_EXECUTABLE ||
                       ctx->targets[i].kind == BM_TARGET_SHARED_LIBRARY ||
                       ctx->targets[i].kind == BM_TARGET_MODULE_LIBRARY) {
                needs_require_paths = true;
                if (!ctx->policy.use_compiler_driver_for_executable_link ||
                    !ctx->policy.use_compiler_driver_for_shared_link ||
                    !ctx->policy.use_compiler_driver_for_module_link) {
                    needs_link_tool = true;
                }
            }
        } else if (ctx->targets[i].kind == BM_TARGET_UTILITY && ctx->targets[i].state_path.count > 0) {
            needs_write_stamp = true;
        }
    }

    if (ctx->build_step_count > 0) {
        for (size_t i = 0; i < ctx->build_step_count; ++i) {
            BM_Build_Step_Id id = (BM_Build_Step_Id)i;
            if (bm_query_build_step_outputs(ctx->model, id).count > 0 ||
                bm_query_build_step_byproducts(ctx->model, id).count > 0) {
                needs_require_paths = true;
            }
            if (ctx->build_steps[i].uses_stamp) needs_write_stamp = true;
        }
    }
    if (bm_query_test_count(ctx->model) > 0) ctx->helper_bits |= CG_HELPER_RUN_CMD;
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

    if (bm_query_cpack_package_count(ctx->model) > 0) {
        needs_install_copy_file = true;
        needs_install_copy_directory = true;
        needs_package_archive = true;
        if (cg_package_generator_enabled(ctx, "TGZ")) ctx->helper_bits |= CG_HELPER_GZIP_RESOLVER;
        if (cg_package_generator_enabled(ctx, "TXZ")) ctx->helper_bits |= CG_HELPER_XZ_RESOLVER;
    }

    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        BM_Replay_Opcode opcode = bm_query_replay_action_opcode(ctx->model, id);
        needs_write_stamp = true;
        if (opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR ||
            opcode == BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR ||
            opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE) {
            needs_tar_resolver = true;
        }
        if (opcode == BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL ||
            opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE) {
            BM_String_Span argv = bm_query_replay_action_argv(ctx->model, id);
            size_t hash_algo_index = opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE ? 1u : 0u;
            size_t hash_value_index = opcode == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE ? 2u : 1u;
            if (argv.count > hash_value_index &&
                argv.items[hash_algo_index].count > 0 &&
                argv.items[hash_value_index].count > 0) {
                needs_replay_sha256 = true;
            }
        }
        if (bm_query_replay_action_phase(ctx->model, id) == BM_REPLAY_PHASE_TEST) {
            ctx->helper_bits |= CG_HELPER_RUN_CMD;
        }
    }

    if (needs_compile_toolchain) ctx->helper_bits |= CG_HELPER_COMPILE_TOOLCHAIN;
    if (needs_archive_tool) ctx->helper_bits |= CG_HELPER_ARCHIVE_TOOL;
    if (needs_link_tool) ctx->helper_bits |= CG_HELPER_LINK_TOOL;
    if (needs_require_paths) ctx->helper_bits |= CG_HELPER_REQUIRE_PATHS;
    if (needs_write_stamp) ctx->helper_bits |= CG_HELPER_WRITE_STAMP;
    if (needs_install_copy_file) ctx->helper_bits |= CG_HELPER_INSTALL_COPY_FILE;
    if (needs_install_copy_directory) ctx->helper_bits |= CG_HELPER_INSTALL_COPY_DIRECTORY;
    if (needs_package_archive) ctx->helper_bits |= CG_HELPER_PACKAGE_ARCHIVE;
    if (needs_tar_resolver) ctx->helper_bits |= CG_HELPER_TAR_RESOLVER;
    if (needs_replay_sha256) ctx->helper_bits |= CG_HELPER_REPLAY_SHA256;
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
    if (ctx->helper_bits & CG_HELPER_GZIP_RESOLVER) {
        if (!cg_emit_preferred_tool_resolver(out,
                                             "resolve_gzip_bin",
                                             "NOB_GZIP_BIN",
                                             ctx->embedded_gzip_bin_abs,
                                             "gzip")) {
            return false;
        }
    }
    if (ctx->helper_bits & CG_HELPER_XZ_RESOLVER) {
        if (!cg_emit_preferred_tool_resolver(out,
                                             "resolve_xz_bin",
                                             "NOB_XZ_BIN",
                                             ctx->embedded_xz_bin_abs,
                                             "xz")) {
            return false;
        }
    }
    if (ctx->helper_bits & CG_HELPER_TAR_RESOLVER) {
        nob_sb_append_cstr(out,
            "static const char *resolve_tar_bin(void) {\n"
            "    const char *tool = getenv(\"NOB_TAR_BIN\");\n"
            "    if (tool && tool[0] != '\\0') return tool;\n"
            "    return \"tar\";\n"
            "}\n\n");
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
            "static bool __attribute__((unused)) ensure_parent_dir(const char *path) {\n"
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
            "}\n\n"
            "static char *read_text_file_dup(const char *path) {\n"
            "    Nob_String_Builder sb = {0};\n"
            "    char *copy = NULL;\n"
            "    if (!path || path[0] == '\\0' || !nob_file_exists(path)) {\n"
            "        copy = (char *)malloc(1u);\n"
            "        if (!copy) return NULL;\n"
            "        copy[0] = '\\0';\n"
            "        return copy;\n"
            "    }\n"
            "    if (!nob_read_entire_file(path, &sb)) {\n"
            "        nob_sb_free(sb);\n"
            "        return NULL;\n"
            "    }\n"
            "    copy = (char *)malloc(sb.count + 1u);\n"
            "    if (!copy) {\n"
            "        nob_sb_free(sb);\n"
            "        return NULL;\n"
            "    }\n"
            "    if (sb.count > 0 && sb.items) memcpy(copy, sb.items, sb.count);\n"
            "    copy[sb.count] = '\\0';\n"
            "    nob_sb_free(sb);\n"
            "    return copy;\n"
            "}\n\n"
            "static bool run_cmd_capture_in_dir(const char *working_dir,\n"
            "                                   Nob_Cmd *cmd,\n"
            "                                   char **out_stdout,\n"
            "                                   char **out_stderr) {\n"
            "    static size_t capture_index = 0;\n"
            "    const char *launch_dir = NULL;\n"
            "    const char *saved_dir = NULL;\n"
            "    const char *capture_root = NULL;\n"
            "    const char *stdout_path = NULL;\n"
            "    const char *stderr_path = NULL;\n"
            "    char *stdout_text = NULL;\n"
            "    char *stderr_text = NULL;\n"
            "    bool ok = false;\n"
            "    if (out_stdout) *out_stdout = NULL;\n"
            "    if (out_stderr) *out_stderr = NULL;\n"
            "    launch_dir = nob_get_current_dir_temp();\n"
            "    if (!launch_dir) return false;\n"
            "    launch_dir = nob_temp_strdup(launch_dir);\n"
            "    if (!launch_dir) return false;\n"
            "    capture_root = nob_temp_sprintf(\"%s/.nob/captures\", launch_dir);\n"
            "    if (!capture_root) return false;\n"
            "    if (!ensure_dir(capture_root)) return false;\n"
            "    stdout_path = nob_temp_sprintf(\"%s/stdout-%zu.txt\", capture_root, capture_index);\n"
            "    stderr_path = nob_temp_sprintf(\"%s/stderr-%zu.txt\", capture_root, capture_index);\n"
            "    capture_index++;\n"
            "    if (working_dir && working_dir[0] != '\\0') {\n"
            "        saved_dir = launch_dir;\n"
            "        if (!nob_set_current_dir(working_dir)) return false;\n"
            "    }\n"
            "    ok = nob_cmd_run(cmd, .stdout_path = stdout_path, .stderr_path = stderr_path);\n"
            "    if (working_dir && working_dir[0] != '\\0') {\n"
            "        if (!nob_set_current_dir(saved_dir)) return false;\n"
            "    }\n"
            "    stdout_text = read_text_file_dup(stdout_path);\n"
            "    stderr_text = read_text_file_dup(stderr_path);\n"
            "    if (stdout_path && nob_file_exists(stdout_path)) (void)remove(stdout_path);\n"
            "    if (stderr_path && nob_file_exists(stderr_path)) (void)remove(stderr_path);\n"
            "    if (!stdout_text || !stderr_text) {\n"
            "        free(stdout_text);\n"
            "        free(stderr_text);\n"
            "        return false;\n"
            "    }\n"
            "    if (out_stdout) *out_stdout = stdout_text; else free(stdout_text);\n"
            "    if (out_stderr) *out_stderr = stderr_text; else free(stderr_text);\n"
            "    return ok;\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_WRITE_STAMP) {
        nob_sb_append_cstr(out,
            "static bool write_stamp(const char *path) {\n"
            "    if (!ensure_parent_dir(path)) return false;\n"
            "    return nob_write_entire_file(path, \"\", 0);\n"
            "}\n\n");
    }

    if (ctx->helper_bits & CG_HELPER_REQUIRE_PATHS) {
        nob_sb_append_cstr(out,
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
