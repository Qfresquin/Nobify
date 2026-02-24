#include "eval_try_compile.h"

#include "evaluator_internal.h"
#include "eval_opt_parser.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static String_View sv_dirname(String_View path) {
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            if (i == 0) return nob_sv_from_parts(path.data, 1);
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr(".");
}

static bool file_exists_sv(Evaluator_Context *ctx, String_View path) {
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static String_View sv_concat_suffix_temp(Evaluator_Context *ctx, String_View base, const char *suffix) {
    if (!ctx || !suffix) return nob_sv_from_cstr("");
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), base.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (base.count) memcpy(buf, base.data, base.count);
    if (suffix_len) memcpy(buf + base.count, suffix, suffix_len);
    buf[base.count + suffix_len] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool sv_list_push_temp(Evaluator_Context *ctx, SV_List *list, String_View sv) {
    if (!ctx || !list) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return ctx_oom(ctx);
    }
    list->items[list->count++] = sv;
    return true;
}

static String_View sv_join_space_temp(Evaluator_Context *ctx, const String_View *items, size_t count) {
    if (!ctx || !items || count == 0) return nob_sv_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += items[i].count;
        if (i + 1 < count) total += 1;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].count > 0) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
        if (i + 1 < count) {
            buf[off++] = ' ';
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool mkdir_p_local(const char *path) {
    if (!path) return false;
    size_t len0 = strlen(path);
    char *tmp = (char*)malloc(len0 + 1);
    if (!tmp) return false;
    memcpy(tmp, path, len0 + 1);
    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        free(tmp);
        return false;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            if ((p == tmp + 2) && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') continue;
            *p = '\0';
            (void)nob_mkdir_if_not_exists(tmp);
            *p = '/';
        }
    }
    bool ok = nob_mkdir_if_not_exists(tmp);
    free(tmp);
    return ok;
}

static bool try_compile_noop_positional(Evaluator_Context *ctx,
                                        void *userdata,
                                        String_View value,
                                        size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

enum {
    TRY_COMPILE_OPT_OUTPUT_VARIABLE = 1,
    TRY_COMPILE_OPT_COPY_FILE,
    TRY_COMPILE_OPT_COPY_FILE_ERROR,
    TRY_COMPILE_OPT_SOURCE_FROM_CONTENT,
    TRY_COMPILE_OPT_SOURCE_FROM_VAR,
    TRY_COMPILE_OPT_SOURCE_FROM_FILE,
    TRY_COMPILE_OPT_SOURCES,
    TRY_COMPILE_OPT_CMAKE_FLAGS,
    TRY_COMPILE_OPT_COMPILE_DEFINITIONS,
    TRY_COMPILE_OPT_LINK_OPTIONS,
    TRY_COMPILE_OPT_LINK_LIBRARIES,
    TRY_COMPILE_OPT_NO_CACHE,
    TRY_COMPILE_OPT_LOG_DESCRIPTION,
};

typedef struct {
    String_View bindir;
    String_View current_src_dir;
    String_View current_bin_dir;
    SV_List sources;
    String_View output_var;
    String_View copy_file_path;
    String_View copy_file_error_var;
    String_View log_description;
    bool no_cache;
} Try_Compile_Option_State;

static bool try_compile_is_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE") ||
           eval_sv_eq_ci_lit(tok, "COPY_FILE") ||
           eval_sv_eq_ci_lit(tok, "COPY_FILE_ERROR") ||
           eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS") ||
           eval_sv_eq_ci_lit(tok, "COMPILE_DEFINITIONS") ||
           eval_sv_eq_ci_lit(tok, "LINK_OPTIONS") ||
           eval_sv_eq_ci_lit(tok, "LINK_LIBRARIES") ||
           eval_sv_eq_ci_lit(tok, "NO_CACHE") ||
           eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION") ||
           eval_sv_eq_ci_lit(tok, "SOURCES") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_CONTENT") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_VAR") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_FROM_FILE") ||
           eval_sv_eq_ci_lit(tok, "PROJECT") ||
           eval_sv_eq_ci_lit(tok, "SOURCE_DIR") ||
           eval_sv_eq_ci_lit(tok, "BINARY_DIR") ||
           eval_sv_eq_ci_lit(tok, "TARGET");
}

static String_View try_compile_current_src_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    return v.count > 0 ? v : ctx->source_dir;
}

static String_View try_compile_current_bin_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static bool try_compile_parse_cmake_flag_define(Evaluator_Context *ctx, String_View def) {
    if (!ctx) return false;
    if (!(def.count > 2 && def.data[0] == '-' && def.data[1] == 'D')) return true;

    String_View kv = nob_sv_from_parts(def.data + 2, def.count - 2);
    if (kv.count == 0) return true;

    const char *eq = memchr(kv.data, '=', kv.count);
    String_View raw_key = kv;
    String_View val = nob_sv_from_cstr("1");
    if (eq) {
        size_t klen = (size_t)(eq - kv.data);
        raw_key = nob_sv_from_parts(kv.data, klen);
        val = nob_sv_from_parts(eq + 1, kv.count - klen - 1);
    }

    const char *colon = memchr(raw_key.data, ':', raw_key.count);
    String_View key = raw_key;
    if (colon) {
        key = nob_sv_from_parts(raw_key.data, (size_t)(colon - raw_key.data));
    }
    if (key.count == 0) return true;

    return eval_var_set(ctx, key, val);
}

static bool try_compile_emit_cache_result_if_needed(Evaluator_Context *ctx,
                                                    Cmake_Event_Origin o,
                                                    String_View out_var,
                                                    String_View result,
                                                    bool no_cache) {
    if (!ctx || no_cache) return true;
    Cmake_Event ce = {0};
    ce.kind = EV_SET_CACHE_ENTRY;
    ce.origin = o;
    ce.as.cache_entry.key = sv_copy_to_event_arena(ctx, out_var);
    ce.as.cache_entry.value = sv_copy_to_event_arena(ctx, result);
    return emit_event(ctx, ce);
}

static bool try_compile_add_source(Evaluator_Context *ctx, Try_Compile_Option_State *st, String_View source) {
    if (!ctx || !st) return false;
    return sv_list_push_temp(ctx, &st->sources, source);
}

static bool try_compile_write_generated_source(Evaluator_Context *ctx,
                                               Try_Compile_Option_State *st,
                                               String_View name,
                                               String_View content) {
    if (!ctx || !st) return false;
    if (name.count == 0) name = nob_sv_from_cstr("try_compile_source.c");
    String_View src_path = eval_sv_path_join(eval_temp_arena(ctx), st->bindir, name);
    char *src_path_c = eval_sv_to_cstr_temp(ctx, src_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_path_c, false);
    if (!nob_write_entire_file(src_path_c, content.data ? content.data : "", content.count)) {
        return false;
    }
    return try_compile_add_source(ctx, st, src_path);
}

static bool try_compile_on_option(Evaluator_Context *ctx,
                                  void *userdata,
                                  int id,
                                  SV_List values,
                                  size_t token_index) {
    (void)token_index;
    if (!ctx || !userdata) return false;
    Try_Compile_Option_State *st = (Try_Compile_Option_State*)userdata;
    switch (id) {
    case TRY_COMPILE_OPT_OUTPUT_VARIABLE:
        if (values.count > 0) st->output_var = values.items[0];
        return true;
    case TRY_COMPILE_OPT_COPY_FILE:
        if (values.count > 0) st->copy_file_path = values.items[0];
        return true;
    case TRY_COMPILE_OPT_COPY_FILE_ERROR:
        if (values.count > 0) st->copy_file_error_var = values.items[0];
        return true;
    case TRY_COMPILE_OPT_SOURCE_FROM_CONTENT:
        if (values.count == 0) return true;
        return try_compile_write_generated_source(
            ctx,
            st,
            values.items[0],
            values.count > 1
                ? sv_join_space_temp(ctx, &values.items[1], values.count - 1)
                : nob_sv_from_cstr(""));
    case TRY_COMPILE_OPT_SOURCE_FROM_VAR:
        if (values.count < 2) return true;
        return try_compile_write_generated_source(ctx, st, values.items[0], eval_var_get(ctx, values.items[1]));
    case TRY_COMPILE_OPT_SOURCE_FROM_FILE:
        if (values.count < 2) return true;
        {
            String_View src_file = values.items[1];
            if (!eval_sv_is_abs_path(src_file)) {
                src_file = eval_sv_path_join(eval_temp_arena(ctx), st->current_src_dir, src_file);
            }
            char *src_file_c = eval_sv_to_cstr_temp(ctx, src_file);
            EVAL_OOM_RETURN_IF_NULL(ctx, src_file_c, false);

            Nob_String_Builder sb = {0};
            if (!nob_read_entire_file(src_file_c, &sb)) {
                return true;
            }
            String_View content = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
            bool ok = try_compile_write_generated_source(ctx, st, values.items[0], content);
            nob_sb_free(sb);
            return ok;
        }
    case TRY_COMPILE_OPT_SOURCES:
        for (size_t i = 0; i < values.count; i++) {
            if (!try_compile_add_source(ctx, st, values.items[i])) return false;
        }
        return true;
    case TRY_COMPILE_OPT_CMAKE_FLAGS:
        for (size_t i = 0; i < values.count; i++) {
            if (!try_compile_parse_cmake_flag_define(ctx, values.items[i])) return false;
        }
        return true;
    case TRY_COMPILE_OPT_COMPILE_DEFINITIONS:
    case TRY_COMPILE_OPT_LINK_OPTIONS:
    case TRY_COMPILE_OPT_LINK_LIBRARIES:
        return true;
    case TRY_COMPILE_OPT_NO_CACHE:
        st->no_cache = true;
        return true;
    case TRY_COMPILE_OPT_LOG_DESCRIPTION:
        if (values.count > 0) st->log_description = values.items[0];
        return true;
    default:
        return true;
    }
}

bool eval_handle_try_compile(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("try_compile() requires at least a result variable"),
                       nob_sv_from_cstr("Usage: try_compile(<out-var> <bindir> <src> ...)"));
        return !eval_should_stop(ctx);
    }

    String_View out_var = a.items[0];

    if (a.count >= 2 && eval_sv_eq_ci_lit(a.items[1], "PROJECT")) {
        String_View current_src = try_compile_current_src_dir(ctx);
        String_View current_bin = try_compile_current_bin_dir(ctx);
        String_View project_name = nob_sv_from_cstr("");
        String_View source_dir_raw = nob_sv_from_cstr("");
        String_View binary_dir_raw = nob_sv_from_cstr("");
        String_View target_name = nob_sv_from_cstr("");
        String_View output_var = nob_sv_from_cstr("");
        String_View log_description = nob_sv_from_cstr("");
        bool no_cache = false;

        for (size_t i = 1; i < a.count; i++) {
            String_View tok = a.items[i];
            if (eval_sv_eq_ci_lit(tok, "PROJECT")) {
                if (i + 1 < a.count) project_name = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "SOURCE_DIR")) {
                if (i + 1 < a.count) source_dir_raw = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "BINARY_DIR")) {
                if (i + 1 < a.count) binary_dir_raw = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "TARGET")) {
                if (i + 1 < a.count) target_name = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE")) {
                if (i + 1 < a.count) output_var = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION")) {
                if (i + 1 < a.count) log_description = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "NO_CACHE")) {
                no_cache = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS")) {
                while (i + 1 < a.count && !try_compile_is_keyword(a.items[i + 1])) {
                    i++;
                    if (!try_compile_parse_cmake_flag_define(ctx, a.items[i])) return !eval_should_stop(ctx);
                }
                continue;
            }
        }

        if (source_dir_raw.count == 0) {
            eval_emit_diag(ctx,
                           EV_DIAG_ERROR,
                           nob_sv_from_cstr("dispatcher"),
                           node->as.cmd.name,
                           o,
                           nob_sv_from_cstr("try_compile(PROJECT ...) requires SOURCE_DIR"),
                           nob_sv_from_cstr("Usage: try_compile(<out-var> PROJECT <name> SOURCE_DIR <dir> ...)"));
            return !eval_should_stop(ctx);
        }

        String_View source_dir = source_dir_raw;
        if (!eval_sv_is_abs_path(source_dir)) {
            source_dir = eval_sv_path_join(eval_temp_arena(ctx), current_src, source_dir);
        }
        String_View bindir = binary_dir_raw.count > 0 ? binary_dir_raw : nob_sv_from_cstr("CMakeFiles/CMakeTmp");
        if (!eval_sv_is_abs_path(bindir)) {
            bindir = eval_sv_path_join(eval_temp_arena(ctx), current_bin, bindir);
        }
        char *bindir_c = eval_sv_to_cstr_temp(ctx, bindir);
        EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, !eval_should_stop(ctx));
        (void)mkdir_p_local(bindir_c);

        String_View cmakelists_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));
        bool compile_ok = file_exists_sv(ctx, cmakelists_path);
        String_View result = compile_ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
        (void)eval_var_set(ctx, out_var, result);
        if (!try_compile_emit_cache_result_if_needed(ctx, o, out_var, result, no_cache)) {
            return !eval_should_stop(ctx);
        }

        if (output_var.count > 0) {
            String_View out_msg = compile_ok
                ? (log_description.count > 0 ? log_description : nob_sv_from_cstr("try_compile(PROJECT) simulated success"))
                : sv_concat_suffix_temp(ctx, nob_sv_from_cstr("try_compile project source directory missing CMakeLists.txt: "),
                                        eval_sv_to_cstr_temp(ctx, source_dir_raw));
            (void)eval_var_set(ctx, output_var, out_msg);
        }

        (void)project_name;
        (void)target_name;
        return !eval_should_stop(ctx);
    }

    if (a.count < 3) {
        eval_emit_diag(ctx,
                       EV_DIAG_ERROR,
                       nob_sv_from_cstr("dispatcher"),
                       node->as.cmd.name,
                       o,
                       nob_sv_from_cstr("try_compile() requires bindir and at least one source input"),
                       nob_sv_from_cstr("Usage: try_compile(<out-var> <bindir> <src>|SOURCES ...|SOURCE_FROM_CONTENT ...)"));
        return !eval_should_stop(ctx);
    }

    String_View current_src = try_compile_current_src_dir(ctx);
    String_View current_bin = try_compile_current_bin_dir(ctx);
    String_View bindir = a.items[1];
    if (!eval_sv_is_abs_path(bindir)) {
        bindir = eval_sv_path_join(eval_temp_arena(ctx), current_bin, bindir);
    }
    char *bindir_c = eval_sv_to_cstr_temp(ctx, bindir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, !eval_should_stop(ctx));
    (void)mkdir_p_local(bindir_c);

    Try_Compile_Option_State opt = {
        .bindir = bindir,
        .current_src_dir = current_src,
        .current_bin_dir = current_bin,
        .sources = {0},
        .output_var = nob_sv_from_cstr(""),
        .copy_file_path = nob_sv_from_cstr(""),
        .copy_file_error_var = nob_sv_from_cstr(""),
        .log_description = nob_sv_from_cstr(""),
        .no_cache = false,
    };

    size_t opt_start = 2;
    if (a.count >= 3 && !try_compile_is_keyword(a.items[2])) {
        if (!try_compile_add_source(ctx, &opt, a.items[2])) return !eval_should_stop(ctx);
        opt_start = 3;
    }

    static const Eval_Opt_Spec k_try_compile_specs[] = {
        {TRY_COMPILE_OPT_OUTPUT_VARIABLE, "OUTPUT_VARIABLE", EVAL_OPT_OPTIONAL_SINGLE},
        {TRY_COMPILE_OPT_COPY_FILE, "COPY_FILE", EVAL_OPT_OPTIONAL_SINGLE},
        {TRY_COMPILE_OPT_COPY_FILE_ERROR, "COPY_FILE_ERROR", EVAL_OPT_OPTIONAL_SINGLE},
        {TRY_COMPILE_OPT_SOURCE_FROM_CONTENT, "SOURCE_FROM_CONTENT", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_SOURCE_FROM_VAR, "SOURCE_FROM_VAR", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_SOURCE_FROM_FILE, "SOURCE_FROM_FILE", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_SOURCES, "SOURCES", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_CMAKE_FLAGS, "CMAKE_FLAGS", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_COMPILE_DEFINITIONS, "COMPILE_DEFINITIONS", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_LINK_OPTIONS, "LINK_OPTIONS", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_LINK_LIBRARIES, "LINK_LIBRARIES", EVAL_OPT_MULTI},
        {TRY_COMPILE_OPT_NO_CACHE, "NO_CACHE", EVAL_OPT_FLAG},
        {TRY_COMPILE_OPT_LOG_DESCRIPTION, "LOG_DESCRIPTION", EVAL_OPT_OPTIONAL_SINGLE},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("dispatcher"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             a,
                             opt_start,
                             k_try_compile_specs,
                             NOB_ARRAY_LEN(k_try_compile_specs),
                             cfg,
                             try_compile_on_option,
                             try_compile_noop_positional,
                             &opt)) {
        return !eval_should_stop(ctx);
    }

    bool compile_ok = opt.sources.count > 0;
    String_View first_resolved_source = nob_sv_from_cstr("");
    String_View missing_hint = nob_sv_from_cstr("");
    for (size_t i = 0; i < opt.sources.count; i++) {
        String_View src = opt.sources.items[i];
        if (!eval_sv_is_abs_path(src)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), current_src, src);
        }
        if (first_resolved_source.count == 0) first_resolved_source = src;
        if (!file_exists_sv(ctx, src)) {
            compile_ok = false;
            missing_hint = opt.sources.items[i];
            break;
        }
    }
    if (opt.sources.count == 0) {
        compile_ok = false;
    }

    String_View result = compile_ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
    (void)eval_var_set(ctx, out_var, result);
    if (!try_compile_emit_cache_result_if_needed(ctx, o, out_var, result, opt.no_cache)) {
        return !eval_should_stop(ctx);
    }

    if (compile_ok && opt.copy_file_path.count > 0) {
        String_View dst = opt.copy_file_path;
        if (!eval_sv_is_abs_path(dst)) {
            dst = eval_sv_path_join(eval_temp_arena(ctx), current_bin, dst);
        }
        String_View dst_parent = sv_dirname(dst);
        char *dst_parent_c = eval_sv_to_cstr_temp(ctx, dst_parent);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_parent_c, !eval_should_stop(ctx));
        (void)mkdir_p_local(dst_parent_c);

        char *src_c = eval_sv_to_cstr_temp(ctx, first_resolved_source);
        char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
        EVAL_OOM_RETURN_IF_NULL(ctx, src_c, !eval_should_stop(ctx));
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, !eval_should_stop(ctx));
        bool copy_ok = nob_copy_file(src_c, dst_c);
        if (!copy_ok && opt.copy_file_error_var.count > 0) {
            (void)eval_var_set(ctx, opt.copy_file_error_var, nob_sv_from_cstr("try_compile COPY_FILE failed"));
        } else if (copy_ok && opt.copy_file_error_var.count > 0) {
            (void)eval_var_set(ctx, opt.copy_file_error_var, nob_sv_from_cstr(""));
        }
    }

    if (opt.output_var.count > 0) {
        String_View out_msg = compile_ok
            ? (opt.log_description.count > 0 ? opt.log_description : nob_sv_from_cstr("try_compile simulated success"))
            : (missing_hint.count > 0
                ? sv_concat_suffix_temp(ctx, nob_sv_from_cstr("try_compile source file not found: "),
                                        eval_sv_to_cstr_temp(ctx, missing_hint))
                : nob_sv_from_cstr("try_compile requires at least one source"));
        (void)eval_var_set(ctx, opt.output_var, out_msg);
    }

    return !eval_should_stop(ctx);
}
