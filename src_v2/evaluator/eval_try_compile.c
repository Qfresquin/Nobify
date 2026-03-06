#include "eval_try_compile_internal.h"

#include <sys/stat.h>

bool try_compile_file_exists_sv(Evaluator_Context *ctx, String_View path) {
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

bool try_compile_mkdir_p_local(Evaluator_Context *ctx, const char *path) {
    if (!ctx || !path) return false;
    size_t len0 = strlen(path);
    char *tmp = (char*)arena_alloc(eval_temp_arena(ctx), len0 + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, tmp, false);
    memcpy(tmp, path, len0 + 1);
    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    if (len == 0) return false;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            if ((p == tmp + 2) && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') continue;
            *p = '\0';
            (void)nob_mkdir_if_not_exists(tmp);
            *p = '/';
        }
    }
    return nob_mkdir_if_not_exists(tmp);
}

String_View try_compile_current_src_dir(Evaluator_Context *ctx) {
    return eval_current_source_dir(ctx);
}

String_View try_compile_current_bin_dir(Evaluator_Context *ctx) {
    return eval_current_binary_dir(ctx);
}

String_View try_compile_concat_prefix_temp(Evaluator_Context *ctx, const char *prefix, String_View tail) {
    if (!ctx || !prefix) return nob_sv_from_cstr("");
    size_t prefix_len = strlen(prefix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + tail.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (prefix_len > 0) memcpy(buf, prefix, prefix_len);
    if (tail.count > 0) memcpy(buf + prefix_len, tail.data, tail.count);
    buf[prefix_len + tail.count] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View try_compile_basename(String_View path) {
    size_t start = 0;
    for (size_t i = path.count; i-- > 0;) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            start = i + 1;
            break;
        }
    }
    return nob_sv_from_parts(path.data + start, path.count - start);
}

bool try_compile_is_false(String_View v) {
    return eval_sv_eq_ci_lit(v, "0") ||
           eval_sv_eq_ci_lit(v, "OFF") ||
           eval_sv_eq_ci_lit(v, "NO") ||
           eval_sv_eq_ci_lit(v, "FALSE") ||
           eval_sv_eq_ci_lit(v, "N") ||
           eval_sv_eq_ci_lit(v, "IGNORE") ||
           eval_sv_eq_ci_lit(v, "NOTFOUND") ||
           (v.count >= 9 && eval_sv_eq_ci_lit(nob_sv_from_parts(v.data + v.count - 9, 9), "-NOTFOUND"));
}

bool try_compile_source_push(Evaluator_Context *ctx,
                             Try_Compile_Source_List *list,
                             Try_Compile_Source_Item item) {
    if (!ctx || !list) return false;
    if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), list->items, item)) return false;
    list->count = arena_arr_len(list->items);
    list->capacity = arena_arr_cap(list->items);
    return true;
}

bool try_compile_keyword_is_standard(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "C_STANDARD") ||
           eval_sv_eq_ci_lit(tok, "C_STANDARD_REQUIRED") ||
           eval_sv_eq_ci_lit(tok, "C_EXTENSIONS") ||
           eval_sv_eq_ci_lit(tok, "CXX_STANDARD") ||
           eval_sv_eq_ci_lit(tok, "CXX_STANDARD_REQUIRED") ||
           eval_sv_eq_ci_lit(tok, "CXX_EXTENSIONS");
}

bool try_compile_is_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE") ||
           eval_sv_eq_ci_lit(tok, "COPY_FILE") ||
           eval_sv_eq_ci_lit(tok, "COPY_FILE_ERROR") ||
           eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS") ||
           eval_sv_eq_ci_lit(tok, "COMPILE_DEFINITIONS") ||
           eval_sv_eq_ci_lit(tok, "LINK_OPTIONS") ||
           eval_sv_eq_ci_lit(tok, "LINK_LIBRARIES") ||
           eval_sv_eq_ci_lit(tok, "LINKER_LANGUAGE") ||
           eval_sv_eq_ci_lit(tok, "NO_CACHE") ||
           eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION") ||
           eval_sv_eq_ci_lit(tok, "SOURCES") ||
           eval_sv_eq_ci_lit(tok, "SOURCES_TYPE") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_CONTENT") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_VAR") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_FILE") ||
           eval_sv_eq_ci_lit(tok, "PROJECT") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_DIR") ||
           eval_sv_eq_ci_lit(tok, "BINARY_DIR") ||
           eval_sv_eq_ci_lit(tok, "TARGET") ||
           try_compile_keyword_is_standard(tok);
}

Try_Compile_Language try_compile_language_from_sources_type(String_View tok) {
    if (eval_sv_eq_ci_lit(tok, "C")) return TRY_COMPILE_LANG_C;
    if (eval_sv_eq_ci_lit(tok, "CXX")) return TRY_COMPILE_LANG_CXX;
    if (eval_sv_eq_ci_lit(tok, "HEADERS")) return TRY_COMPILE_LANG_HEADERS;
    return TRY_COMPILE_LANG_AUTO;
}

Try_Compile_Language try_compile_detect_language(String_View path) {
    String_View base = try_compile_basename(path);
    const char *dot = NULL;
    for (size_t i = base.count; i-- > 0;) {
        if (base.data[i] == '.') {
            dot = base.data + i;
            break;
        }
        if (base.data[i] == '/' || base.data[i] == '\\') break;
    }
    if (!dot) return TRY_COMPILE_LANG_AUTO;

    String_View ext = nob_sv_from_cstr(dot);
    if (eval_sv_eq_ci_lit(ext, ".c")) return TRY_COMPILE_LANG_C;
    if (eval_sv_eq_ci_lit(ext, ".cc") ||
        eval_sv_eq_ci_lit(ext, ".cpp") ||
        eval_sv_eq_ci_lit(ext, ".cxx") ||
        eval_sv_eq_ci_lit(ext, ".c++")) return TRY_COMPILE_LANG_CXX;
    if (eval_sv_eq_ci_lit(ext, ".h") ||
        eval_sv_eq_ci_lit(ext, ".hh") ||
        eval_sv_eq_ci_lit(ext, ".hpp") ||
        eval_sv_eq_ci_lit(ext, ".hxx")) return TRY_COMPILE_LANG_HEADERS;
    return TRY_COMPILE_LANG_AUTO;
}

String_View try_compile_make_scratch_dir(Evaluator_Context *ctx, String_View current_bin) {
    static size_t s_scratch_counter = 0;
    s_scratch_counter++;
    String_View base = eval_sv_path_join(eval_temp_arena(ctx),
                                         current_bin,
                                         nob_sv_from_cstr("CMakeFiles/CMakeScratch"));
    char *base_c = eval_sv_to_cstr_temp(ctx, base);
    EVAL_OOM_RETURN_IF_NULL(ctx, base_c, nob_sv_from_cstr(""));
    (void)try_compile_mkdir_p_local(ctx, base_c);

    String_View name = sv_copy_to_arena(eval_temp_arena(ctx),
                                        nob_sv_from_cstr(nob_temp_sprintf("try_compile_%zu", s_scratch_counter)));
    return eval_sv_path_join(eval_temp_arena(ctx), base, name);
}

String_View try_compile_resolve_in_dir(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    if (path.count == 0) return path;
    if (eval_sv_is_abs_path(path)) return path;
    return eval_sv_path_join(eval_temp_arena(ctx), base_dir, path);
}

bool try_compile_append_file_to_log(Evaluator_Context *ctx,
                                    const char *path,
                                    Nob_String_Builder *log) {
    if (!ctx || !path || !log) return false;
    if (nob_file_exists(path) == 0) return true;

    struct stat st = {0};
    if (stat(path, &st) == 0 && st.st_size == 0) return true;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return true;
    if (sb.count > 0) {
        nob_sb_append_buf(log, sb.items, sb.count);
        if (log->count > 0 && log->items[log->count - 1] != '\n') nob_sb_append(log, '\n');
    }
    nob_sb_free(sb);
    return true;
}

bool try_compile_run_command_captured(Evaluator_Context *ctx,
                                      Nob_Cmd *cmd,
                                      String_View bindir,
                                      Nob_String_Builder *log,
                                      bool *out_ok) {
    static size_t s_cmd_counter = 0;
    if (!ctx || !cmd || !log) return false;
    s_cmd_counter++;

    String_View out_path = eval_sv_path_join(eval_temp_arena(ctx),
                                             bindir,
                                             sv_copy_to_arena(eval_temp_arena(ctx),
                                                              nob_sv_from_cstr(nob_temp_sprintf("try_compile_cmd_%zu.out",
                                                                                               s_cmd_counter))));
    String_View err_path = eval_sv_path_join(eval_temp_arena(ctx),
                                             bindir,
                                             sv_copy_to_arena(eval_temp_arena(ctx),
                                                              nob_sv_from_cstr(nob_temp_sprintf("try_compile_cmd_%zu.err",
                                                                                               s_cmd_counter))));
    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    char *err_c = eval_sv_to_cstr_temp(ctx, err_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, err_c, false);

    bool ok = nob_cmd_run(cmd, .stdout_path = out_c, .stderr_path = err_c);
    if (!try_compile_append_file_to_log(ctx, out_c, log)) return false;
    if (!try_compile_append_file_to_log(ctx, err_c, log)) return false;
    (void)nob_delete_file(out_c);
    (void)nob_delete_file(err_c);

    if (out_ok) *out_ok = ok;
    return true;
}

String_View try_compile_finish_log(Evaluator_Context *ctx, Nob_String_Builder *log) {
    if (!ctx || !log || log->count == 0) return nob_sv_from_cstr("");
    String_View out = sv_copy_to_arena(eval_temp_arena(ctx), nob_sv_from_parts(log->items, log->count));
    nob_sb_free(*log);
    *log = (Nob_String_Builder){0};
    return out;
}

Eval_Result eval_handle_try_compile(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_fatal();

    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Try_Compile_Request req = {0};
    if (!try_compile_parse_request(ctx, node, &args, &req)) {
        return eval_result_from_ctx(ctx);
    }

    return try_compile_execute_and_publish(ctx, node, &req);
}
