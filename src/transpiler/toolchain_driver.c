#include "toolchain_driver.h"

#include "sys_utils.h"

#include <ctype.h>
#include <string.h>

static bool tc_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

static void tc_append_quoted(String_Builder *sb, String_View value) {
    sb_append(sb, '"');
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == '"') sb_append(sb, '\\');
        sb_append(sb, value.data[i]);
    }
    sb_append(sb, '"');
}

static bool tc_has_whitespace(String_View value) {
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == ' ' || value.data[i] == '\t') return true;
    }
    return false;
}

static void tc_append_command_program(String_Builder *sb, String_View program) {
#if defined(_WIN32)
    if (tc_has_whitespace(program)) {
        sb_append_cstr(sb, "call ");
        tc_append_quoted(sb, program);
    } else {
        sb_append_buf(sb, program.data, program.count);
    }
#else
    if (tc_has_whitespace(program)) {
        tc_append_quoted(sb, program);
    } else {
        sb_append_buf(sb, program.data, program.count);
    }
#endif
}

static String_View tc_path_for_gnu(Arena *arena, String_View path) {
#if defined(_WIN32)
    String_View copy = sv_from_cstr(arena_strndup(arena, path.data, path.count));
    for (size_t i = 0; i < copy.count; i++) {
        if (copy.data[i] == '\\') {
            ((char *)copy.data)[i] = '/';
        }
    }
    return copy;
#else
    (void)arena;
    return path;
#endif
}

static String_View tc_build_dir(const Toolchain_Driver *drv) {
    if (!drv || drv->build_dir.count == 0) return sv_from_cstr(".");
    return drv->build_dir;
}

static String_View tc_make_probe_base_path(const Toolchain_Driver *drv, const char *prefix) {
    static size_t s_probe_counter = 0;
    s_probe_counter++;

    String_View probe_dir = build_path_join(drv->arena, tc_build_dir(drv), sv_from_cstr(".cmk2nob_probes"));
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(probe_dir));

    return build_path_join(
        drv->arena,
        probe_dir,
        sv_from_cstr(nob_temp_sprintf("%s_%zu", prefix, s_probe_counter)));
}

static void tc_cleanup_probe_outputs(String_View base_path, bool msvc) {
    String_View src = sv_from_cstr(nob_temp_sprintf("%s.c", nob_temp_sv_to_cstr(base_path)));
#if defined(_WIN32)
    String_View exe = sv_from_cstr(nob_temp_sprintf("%s.exe", nob_temp_sv_to_cstr(base_path)));
#else
    String_View exe = base_path;
#endif

    if (nob_file_exists(nob_temp_sv_to_cstr(src))) (void)nob_delete_file(nob_temp_sv_to_cstr(src));
    if (nob_file_exists(nob_temp_sv_to_cstr(exe))) (void)nob_delete_file(nob_temp_sv_to_cstr(exe));

    if (msvc) {
        String_View obj = sv_from_cstr(nob_temp_sprintf("%s.obj", nob_temp_sv_to_cstr(base_path)));
        String_View pdb = sv_from_cstr(nob_temp_sprintf("%s.pdb", nob_temp_sv_to_cstr(base_path)));
        if (nob_file_exists(nob_temp_sv_to_cstr(obj))) (void)nob_delete_file(nob_temp_sv_to_cstr(obj));
        if (nob_file_exists(nob_temp_sv_to_cstr(pdb))) (void)nob_delete_file(nob_temp_sv_to_cstr(pdb));
    }
}

String_View toolchain_default_c_compiler(void) {
    const char *env_cc = getenv("CC");
    if (env_cc && env_cc[0] != '\0') return sv_from_cstr(env_cc);
#if defined(_WIN32)
    return sv_from_cstr("gcc");
#else
    return sv_from_cstr("cc");
#endif
}

bool toolchain_compiler_looks_msvc(String_View compiler) {
    String_View base = sys_path_basename(compiler);
    return tc_sv_eq_ci(base, sv_from_cstr("cl")) || tc_sv_eq_ci(base, sv_from_cstr("cl.exe"));
}

bool toolchain_compiler_available(const Toolchain_Driver *drv, String_View compiler) {
    if (!drv || !drv->arena || compiler.count == 0) return false;

    bool timed_out = false;
#if defined(_WIN32)
    String_Builder cmd = {0};
    tc_append_command_program(&cmd, compiler);
    sb_append_cstr(&cmd, " --version >nul 2>nul");
    int rc = sys_run_shell_with_timeout(drv->arena, sb_to_sv(cmd), drv->timeout_ms, &timed_out);
    nob_sb_free(cmd);
    if (rc == 0) return true;

    String_Builder cmd_msvc = {0};
    tc_append_command_program(&cmd_msvc, compiler);
    sb_append_cstr(&cmd_msvc, " /? >nul 2>nul");
    rc = sys_run_shell_with_timeout(drv->arena, sb_to_sv(cmd_msvc), drv->timeout_ms, &timed_out);
    nob_sb_free(cmd_msvc);
    return rc == 0;
#else
    String_Builder cmd = {0};
    tc_append_quoted(&cmd, compiler);
    sb_append_cstr(&cmd, " --version >/dev/null 2>&1");
    int rc = sys_run_shell_with_timeout(drv->arena, sb_to_sv(cmd), drv->timeout_ms, &timed_out);
    nob_sb_free(cmd);
    return rc == 0;
#endif
}

static void tc_append_compiler_command(String_Builder *cmd,
                                       const Toolchain_Driver *drv,
                                       const Toolchain_Compile_Request *req,
                                       bool msvc) {
    String_View src_arg = req->src_path;
    String_View out_arg = req->out_path;
    if (!msvc) {
        src_arg = tc_path_for_gnu(drv->arena, req->src_path);
        out_arg = tc_path_for_gnu(drv->arena, req->out_path);
    }

    tc_append_command_program(cmd, req->compiler);

    const String_List *compile_definitions = req->compile_definitions;
    const String_List *link_options = req->link_options;
    const String_List *link_libraries = req->link_libraries;

    if (msvc) {
        sb_append_cstr(cmd, " /nologo ");
        sb_append_cstr(cmd, "/Fe:");
        tc_append_quoted(cmd, out_arg);
        sb_append(cmd, ' ');
        tc_append_quoted(cmd, src_arg);

        if (compile_definitions) {
            for (size_t i = 0; i < compile_definitions->count; i++) {
                String_View def = compile_definitions->items[i];
                if (def.count == 0) continue;
                sb_append_cstr(cmd, " /D");
                if (nob_sv_starts_with(def, sv_from_cstr("-D")) || nob_sv_starts_with(def, sv_from_cstr("/D"))) {
                    def = nob_sv_from_parts(def.data + 2, def.count - 2);
                }
                tc_append_quoted(cmd, def);
            }
        }

        if ((link_options && link_options->count > 0) || (link_libraries && link_libraries->count > 0)) {
            sb_append_cstr(cmd, " /link");
            if (link_options) {
                for (size_t i = 0; i < link_options->count; i++) {
                    String_View option = link_options->items[i];
                    if (option.count == 0) continue;
                    sb_append(cmd, ' ');
                    tc_append_quoted(cmd, option);
                }
            }
            if (link_libraries) {
                for (size_t i = 0; i < link_libraries->count; i++) {
                    String_View library = link_libraries->items[i];
                    if (library.count == 0) continue;
                    sb_append(cmd, ' ');
                    tc_append_quoted(cmd, library);
                }
            }
        }
        return;
    }

    sb_append_cstr(cmd, " -o ");
    tc_append_quoted(cmd, out_arg);
    sb_append(cmd, ' ');
    tc_append_quoted(cmd, src_arg);

    if (compile_definitions) {
        for (size_t i = 0; i < compile_definitions->count; i++) {
            String_View def = compile_definitions->items[i];
            if (def.count == 0) continue;
            sb_append(cmd, ' ');
            if (nob_sv_starts_with(def, sv_from_cstr("-D")) || nob_sv_starts_with(def, sv_from_cstr("/D"))) {
                sb_append_cstr(cmd, "-D");
                sb_append_buf(cmd, def.data + 2, def.count - 2);
            } else {
                sb_append_cstr(cmd, "-D");
                sb_append_buf(cmd, def.data, def.count);
            }
        }
    }

    if (link_options) {
        for (size_t i = 0; i < link_options->count; i++) {
            String_View option = link_options->items[i];
            if (option.count == 0) continue;
            sb_append(cmd, ' ');
            tc_append_quoted(cmd, option);
        }
    }

    if (link_libraries) {
        for (size_t i = 0; i < link_libraries->count; i++) {
            String_View library = link_libraries->items[i];
            if (library.count == 0) continue;
            if (nob_sv_starts_with(library, sv_from_cstr("-l")) ||
                nob_sv_starts_with(library, sv_from_cstr("-L")) ||
                sys_path_has_separator(library) ||
                nob_sv_end_with(library, ".a") ||
                nob_sv_end_with(library, ".so") ||
                nob_sv_end_with(library, ".dylib") ||
                nob_sv_end_with(library, ".lib")) {
                sb_append(cmd, ' ');
                tc_append_quoted(cmd, library);
            } else {
                sb_append_cstr(cmd, " -l");
                sb_append_buf(cmd, library.data, library.count);
            }
        }
    }
}

bool toolchain_try_compile(const Toolchain_Driver *drv, const Toolchain_Compile_Request *req, Toolchain_Compile_Result *out) {
    if (!drv || !drv->arena || !req || !out) return false;
    if (req->compiler.count == 0 || req->src_path.count == 0 || req->out_path.count == 0) return false;

    out->ok = false;
    out->output = sv_from_cstr("");

    bool msvc = toolchain_compiler_looks_msvc(req->compiler);

    String_Builder cmd = {0};
    tc_append_compiler_command(&cmd, drv, req, msvc);

    String_View base = tc_make_probe_base_path(drv, "try_compile");
    String_View stdout_path = sv_from_cstr(nob_temp_sprintf("%s.out", nob_temp_sv_to_cstr(base)));
    String_View stderr_path = sv_from_cstr(nob_temp_sprintf("%s.err", nob_temp_sv_to_cstr(base)));

    sb_append_cstr(&cmd, " >");
    tc_append_quoted(&cmd, stdout_path);
    sb_append_cstr(&cmd, " 2>");
    tc_append_quoted(&cmd, stderr_path);

    bool timed_out = false;
    int rc = sys_run_shell_with_timeout(drv->arena, sb_to_sv(cmd), drv->timeout_ms, &timed_out);
    nob_sb_free(cmd);

    String_View stdout_text = sys_read_file(drv->arena, stdout_path);
    String_View stderr_text = sys_read_file(drv->arena, stderr_path);
    if (!stdout_text.data) stdout_text = sv_from_cstr("");
    if (!stderr_text.data) stderr_text = sv_from_cstr("");

    if (nob_file_exists(nob_temp_sv_to_cstr(stdout_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stdout_path));
    if (nob_file_exists(nob_temp_sv_to_cstr(stderr_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stderr_path));

    String_Builder merged = {0};
    sb_append_buf(&merged, stdout_text.data, stdout_text.count);
    if (stdout_text.count > 0 && stderr_text.count > 0) sb_append(&merged, '\n');
    sb_append_buf(&merged, stderr_text.data, stderr_text.count);
    out->output = sv_from_cstr(arena_strndup(drv->arena, merged.items ? merged.items : "", merged.count));
    nob_sb_free(merged);

    out->ok = (rc == 0) && !timed_out;
    return true;
}

static String_View tc_run_binary(const Toolchain_Driver *drv, String_View exe_path, const String_List *run_args, int *out_rc) {
    if (!drv || !drv->arena || exe_path.count == 0) {
        if (out_rc) *out_rc = 1;
        return sv_from_cstr("");
    }

    String_Builder cmd = {0};
#if defined(_WIN32)
    sb_append_cstr(&cmd, "call ");
#endif
    tc_append_quoted(&cmd, exe_path);

    if (run_args) {
        for (size_t i = 0; i < run_args->count; i++) {
            String_View arg = run_args->items[i];
            sb_append(&cmd, ' ');
            tc_append_quoted(&cmd, arg);
        }
    }

    String_View base = tc_make_probe_base_path(drv, "try_run_exec");
    String_View stdout_path = sv_from_cstr(nob_temp_sprintf("%s.out", nob_temp_sv_to_cstr(base)));
    String_View stderr_path = sv_from_cstr(nob_temp_sprintf("%s.err", nob_temp_sv_to_cstr(base)));

    sb_append_cstr(&cmd, " >");
    tc_append_quoted(&cmd, stdout_path);
    sb_append_cstr(&cmd, " 2>");
    tc_append_quoted(&cmd, stderr_path);

    bool timed_out = false;
    int rc = sys_run_shell_with_timeout(drv->arena, sb_to_sv(cmd), drv->timeout_ms, &timed_out);
    nob_sb_free(cmd);

    String_View stdout_text = sys_read_file(drv->arena, stdout_path);
    String_View stderr_text = sys_read_file(drv->arena, stderr_path);
    if (!stdout_text.data) stdout_text = sv_from_cstr("");
    if (!stderr_text.data) stderr_text = sv_from_cstr("");

    if (nob_file_exists(nob_temp_sv_to_cstr(stdout_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stdout_path));
    if (nob_file_exists(nob_temp_sv_to_cstr(stderr_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(stderr_path));

    String_Builder merged = {0};
    sb_append_buf(&merged, stdout_text.data, stdout_text.count);
    if (stdout_text.count > 0 && stderr_text.count > 0) sb_append(&merged, '\n');
    sb_append_buf(&merged, stderr_text.data, stderr_text.count);
    String_View output = sv_from_cstr(arena_strndup(drv->arena, merged.items ? merged.items : "", merged.count));
    nob_sb_free(merged);

    if (out_rc) {
        if (timed_out) *out_rc = 124;
        else *out_rc = rc;
    }
    return output;
}

bool toolchain_try_run(const Toolchain_Driver *drv,
                       const Toolchain_Compile_Request *req,
                       const String_List *run_args,
                       Toolchain_Compile_Result *compile_out,
                       int *run_exit_code,
                       String_View *run_output) {
    if (!drv || !drv->arena || !req || !compile_out) return false;

    if (!toolchain_try_compile(drv, req, compile_out)) return false;

    if (!compile_out->ok) {
        if (run_exit_code) *run_exit_code = 1;
        if (run_output) *run_output = sv_from_cstr("");
        return true;
    }

    int rc = 1;
    String_View output = tc_run_binary(drv, req->out_path, run_args, &rc);
    if (run_exit_code) *run_exit_code = rc;
    if (run_output) *run_output = output;

    return true;
}

static bool tc_probe_compiler_available(const Toolchain_Driver *drv, String_View *out_compiler) {
    if (!drv || !drv->arena || !out_compiler) return false;

    String_View compiler = toolchain_default_c_compiler();
    if (!toolchain_compiler_available(drv, compiler)) return false;
    *out_compiler = compiler;
    return true;
}

static void tc_probe_source_with_headers(String_Builder *source, String_View headers) {
    size_t start = 0;
    bool added_any = false;
    for (size_t i = 0; i <= headers.count; i++) {
        bool sep = (i == headers.count) || headers.data[i] == ';';
        if (!sep) continue;

        String_View item = nob_sv_from_parts(headers.data + start, i - start);
        while (item.count > 0 && isspace((unsigned char)item.data[0])) item = nob_sv_from_parts(item.data + 1, item.count - 1);
        while (item.count > 0 && isspace((unsigned char)item.data[item.count - 1])) item = nob_sv_from_parts(item.data, item.count - 1);
        start = i + 1;

        if (item.count == 0) continue;
        added_any = true;

        sb_append_cstr(source, "#include ");
        if ((item.data[0] == '<' && item.data[item.count - 1] == '>') ||
            (item.data[0] == '"' && item.data[item.count - 1] == '"')) {
            sb_append_buf(source, item.data, item.count);
        } else {
            sb_append(source, '<');
            sb_append_buf(source, item.data, item.count);
            sb_append(source, '>');
        }
        sb_append(source, '\n');
    }

    if (!added_any) {
        sb_append_cstr(source, "#include <stddef.h>\n");
    }
}

bool toolchain_probe_check_c_source_compiles(const Toolchain_Driver *drv, String_View source, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!drv || !drv->arena) return false;

    String_View compiler = sv_from_cstr("");
    if (!tc_probe_compiler_available(drv, &compiler)) return false;
    if (used_probe) *used_probe = true;

    bool msvc = toolchain_compiler_looks_msvc(compiler);
    String_View base = tc_make_probe_base_path(drv, "check_c_source_compiles");
    String_View src_path = sv_from_cstr(nob_temp_sprintf("%s.c", nob_temp_sv_to_cstr(base)));
#if defined(_WIN32)
    String_View out_path = sv_from_cstr(nob_temp_sprintf("%s.exe", nob_temp_sv_to_cstr(base)));
#else
    String_View out_path = base;
#endif

    if (!sys_write_file(src_path, source)) {
        tc_cleanup_probe_outputs(base, msvc);
        return false;
    }

    String_List defs = {0};
    String_List opts = {0};
    String_List libs = {0};
    string_list_init(&defs);
    string_list_init(&opts);
    string_list_init(&libs);

    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = &defs,
        .link_options = &opts,
        .link_libraries = &libs,
    };
    Toolchain_Compile_Result result = {0};
    bool ok = toolchain_try_compile(drv, &req, &result) && result.ok;

    tc_cleanup_probe_outputs(base, msvc);
    return ok;
}

bool toolchain_probe_check_c_source_runs(const Toolchain_Driver *drv, String_View source, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!drv || !drv->arena) return false;

    String_View compiler = sv_from_cstr("");
    if (!tc_probe_compiler_available(drv, &compiler)) return false;
    if (used_probe) *used_probe = true;

    bool msvc = toolchain_compiler_looks_msvc(compiler);
    String_View base = tc_make_probe_base_path(drv, "check_c_source_runs");
    String_View src_path = sv_from_cstr(nob_temp_sprintf("%s.c", nob_temp_sv_to_cstr(base)));
#if defined(_WIN32)
    String_View out_path = sv_from_cstr(nob_temp_sprintf("%s.exe", nob_temp_sv_to_cstr(base)));
#else
    String_View out_path = base;
#endif

    if (!sys_write_file(src_path, source)) {
        tc_cleanup_probe_outputs(base, msvc);
        return false;
    }

    String_List defs = {0};
    String_List opts = {0};
    String_List libs = {0};
    string_list_init(&defs);
    string_list_init(&opts);
    string_list_init(&libs);

    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = &defs,
        .link_options = &opts,
        .link_libraries = &libs,
    };

    Toolchain_Compile_Result compile_out = {0};
    if (!toolchain_try_compile(drv, &req, &compile_out) || !compile_out.ok) {
        tc_cleanup_probe_outputs(base, msvc);
        return false;
    }

    int run_rc = 1;
    (void)tc_run_binary(drv, out_path, NULL, &run_rc);
    tc_cleanup_probe_outputs(base, msvc);

    return run_rc == 0;
}

bool toolchain_probe_check_library_exists(const Toolchain_Driver *drv,
                                          String_View library,
                                          String_View function_name,
                                          String_View location,
                                          bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!drv || !drv->arena) return false;

    String_View compiler = sv_from_cstr("");
    if (!tc_probe_compiler_available(drv, &compiler)) return false;
    if (used_probe) *used_probe = true;

    bool msvc = toolchain_compiler_looks_msvc(compiler);
    String_View base = tc_make_probe_base_path(drv, "check_library_exists");
    String_View src_path = sv_from_cstr(nob_temp_sprintf("%s.c", nob_temp_sv_to_cstr(base)));
#if defined(_WIN32)
    String_View out_path = sv_from_cstr(nob_temp_sprintf("%s.exe", nob_temp_sv_to_cstr(base)));
#else
    String_View out_path = base;
#endif

    String_View source = sv_from_cstr(nob_temp_sprintf(
        "extern int %s(void);\nint main(void){ (void)%s; return 0; }\n",
        nob_temp_sv_to_cstr(function_name),
        nob_temp_sv_to_cstr(function_name)));

    if (!sys_write_file(src_path, source)) {
        tc_cleanup_probe_outputs(base, msvc);
        return false;
    }

    String_List defs = {0};
    String_List opts = {0};
    String_List libs = {0};
    string_list_init(&defs);
    string_list_init(&opts);
    string_list_init(&libs);

    if (location.count > 0) {
        if (msvc) {
            String_View opt = sv_from_cstr(nob_temp_sprintf("/LIBPATH:%s", nob_temp_sv_to_cstr(location)));
            string_list_add_unique(&opts, drv->arena, opt);
        } else {
            String_View opt = sv_from_cstr(nob_temp_sprintf("-L%s", nob_temp_sv_to_cstr(location)));
            string_list_add_unique(&opts, drv->arena, opt);
        }
    }
    if (library.count > 0) {
        string_list_add_unique(&libs, drv->arena, library);
    }

    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = &defs,
        .link_options = &opts,
        .link_libraries = &libs,
    };

    Toolchain_Compile_Result compile_out = {0};
    bool ok = toolchain_try_compile(drv, &req, &compile_out) && compile_out.ok;
    tc_cleanup_probe_outputs(base, msvc);
    return ok;
}

bool toolchain_probe_check_symbol_exists(const Toolchain_Driver *drv,
                                         String_View symbol,
                                         String_View headers,
                                         bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!drv || !drv->arena) return false;

    String_View compiler = sv_from_cstr("");
    if (!tc_probe_compiler_available(drv, &compiler)) return false;
    if (used_probe) *used_probe = true;

    bool msvc = toolchain_compiler_looks_msvc(compiler);
    String_View base = tc_make_probe_base_path(drv, "check_symbol_exists");
    String_View src_path = sv_from_cstr(nob_temp_sprintf("%s.c", nob_temp_sv_to_cstr(base)));
#if defined(_WIN32)
    String_View out_path = sv_from_cstr(nob_temp_sprintf("%s.exe", nob_temp_sv_to_cstr(base)));
#else
    String_View out_path = base;
#endif

    String_Builder source = {0};
    tc_probe_source_with_headers(&source, headers);
    sb_append_cstr(&source, "int main(void){ (void)");
    sb_append_buf(&source, symbol.data, symbol.count);
    sb_append_cstr(&source, "; return 0; }\n");

    bool wrote = sys_write_file(src_path, sb_to_sv(source));
    nob_sb_free(source);
    if (!wrote) {
        tc_cleanup_probe_outputs(base, msvc);
        return false;
    }

    String_List defs = {0};
    String_List opts = {0};
    String_List libs = {0};
    string_list_init(&defs);
    string_list_init(&opts);
    string_list_init(&libs);

    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = &defs,
        .link_options = &opts,
        .link_libraries = &libs,
    };

    Toolchain_Compile_Result compile_out = {0};
    bool ok = toolchain_try_compile(drv, &req, &compile_out) && compile_out.ok;
    tc_cleanup_probe_outputs(base, msvc);

    return ok;
}