#include "eval_try_compile.h"

#include "../build_model/build_model_builder.h"
#include "../build_model/build_model_collections.h"
#include "../build_model/build_model_core.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "arena_dyn.h"
#include "stb_ds.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nob__cmd_append(Nob_Cmd *cmd, size_t n, ...);

typedef enum {
    TRY_COMPILE_SIGNATURE_SOURCE = 0,
    TRY_COMPILE_SIGNATURE_PROJECT,
} Try_Compile_Signature;

typedef enum {
    TRY_COMPILE_LANG_AUTO = 0,
    TRY_COMPILE_LANG_C,
    TRY_COMPILE_LANG_CXX,
    TRY_COMPILE_LANG_HEADERS,
} Try_Compile_Language;

typedef enum {
    TRY_COMPILE_BUILD_EXECUTABLE = 0,
    TRY_COMPILE_BUILD_STATIC_LIBRARY,
    TRY_COMPILE_BUILD_SHARED_LIBRARY,
    TRY_COMPILE_BUILD_OBJECTS,
} Try_Compile_Build_Kind;

typedef struct {
    String_View path;
    Try_Compile_Language language;
} Try_Compile_Source_Item;

typedef struct {
    Try_Compile_Source_Item *items;
    size_t count;
    size_t capacity;
} Try_Compile_Source_List;

typedef struct {
    bool has_value;
    String_View standard;
    bool standard_required;
    bool extensions;
    bool extensions_set;
} Try_Compile_Lang_Props;

typedef struct {
    Try_Compile_Signature signature;
    String_View result_var;
    String_View binary_dir;
    String_View current_src_dir;
    String_View current_bin_dir;
    String_View output_var;
    String_View copy_file_path;
    String_View copy_file_error_var;
    String_View log_description;
    String_View source_dir;
    String_View project_name;
    String_View target_name;
    String_View linker_language;
    SV_List compile_definitions;
    SV_List link_options;
    SV_List link_libraries;
    SV_List cmake_flags;
    Try_Compile_Source_List source_items;
    Try_Compile_Lang_Props c_lang;
    Try_Compile_Lang_Props cxx_lang;
    bool no_cache;
} Try_Compile_Request;

typedef struct {
    bool ok;
    String_View output;
    String_View artifact_path;
} Try_Compile_Execution_Result;

typedef struct {
    String_View key;
    String_View value;
} Try_Compile_Target_Artifact;

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool file_exists_sv(Evaluator_Context *ctx, String_View path) {
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static bool mkdir_p_local(Evaluator_Context *ctx, const char *path) {
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

static String_View try_compile_current_src_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    return v.count > 0 ? v : ctx->source_dir;
}

static String_View try_compile_current_bin_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static String_View try_compile_concat_prefix_temp(Evaluator_Context *ctx, const char *prefix, String_View tail) {
    if (!ctx || !prefix) return nob_sv_from_cstr("");
    size_t prefix_len = strlen(prefix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + tail.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (prefix_len > 0) memcpy(buf, prefix, prefix_len);
    if (tail.count > 0) memcpy(buf + prefix_len, tail.data, tail.count);
    buf[prefix_len + tail.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View try_compile_concat_sv_temp(Evaluator_Context *ctx, String_View a, String_View b) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), a.count + b.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (a.count > 0) memcpy(buf, a.data, a.count);
    if (b.count > 0) memcpy(buf + a.count, b.data, b.count);
    buf[a.count + b.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View try_compile_basename(String_View path) {
    size_t start = 0;
    for (size_t i = path.count; i-- > 0;) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            start = i + 1;
            break;
        }
    }
    return nob_sv_from_parts(path.data + start, path.count - start);
}

static bool try_compile_is_false(String_View v) {
    return eval_sv_eq_ci_lit(v, "0") ||
           eval_sv_eq_ci_lit(v, "OFF") ||
           eval_sv_eq_ci_lit(v, "NO") ||
           eval_sv_eq_ci_lit(v, "FALSE") ||
           eval_sv_eq_ci_lit(v, "N") ||
           eval_sv_eq_ci_lit(v, "IGNORE") ||
           eval_sv_eq_ci_lit(v, "NOTFOUND") ||
           (v.count >= 9 && eval_sv_eq_ci_lit(nob_sv_from_parts(v.data + v.count - 9, 9), "-NOTFOUND"));
}

static bool try_compile_sv_push(Evaluator_Context *ctx, SV_List *list, String_View item) {
    if (!ctx || !list) return false;
    if (!arena_da_try_append(eval_temp_arena(ctx), list, item)) return ctx_oom(ctx);
    return true;
}

static bool try_compile_source_push(Evaluator_Context *ctx,
                                    Try_Compile_Source_List *list,
                                    Try_Compile_Source_Item item) {
    if (!ctx || !list) return false;
    if (!arena_da_try_append(eval_temp_arena(ctx), list, item)) return ctx_oom(ctx);
    return true;
}

static bool try_compile_keyword_is_standard(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "C_STANDARD") ||
           eval_sv_eq_ci_lit(tok, "C_STANDARD_REQUIRED") ||
           eval_sv_eq_ci_lit(tok, "C_EXTENSIONS") ||
           eval_sv_eq_ci_lit(tok, "CXX_STANDARD") ||
           eval_sv_eq_ci_lit(tok, "CXX_STANDARD_REQUIRED") ||
           eval_sv_eq_ci_lit(tok, "CXX_EXTENSIONS");
}

static bool try_compile_is_keyword(String_View tok) {
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

static Try_Compile_Language try_compile_language_from_sources_type(String_View tok) {
    if (eval_sv_eq_ci_lit(tok, "C")) return TRY_COMPILE_LANG_C;
    if (eval_sv_eq_ci_lit(tok, "CXX")) return TRY_COMPILE_LANG_CXX;
    if (eval_sv_eq_ci_lit(tok, "HEADERS")) return TRY_COMPILE_LANG_HEADERS;
    return TRY_COMPILE_LANG_AUTO;
}

static Try_Compile_Language try_compile_detect_language(String_View path) {
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

static String_View try_compile_make_scratch_dir(Evaluator_Context *ctx, String_View current_bin) {
    static size_t s_scratch_counter = 0;
    s_scratch_counter++;
    String_View base = eval_sv_path_join(eval_temp_arena(ctx),
                                         current_bin,
                                         nob_sv_from_cstr("CMakeFiles/CMakeScratch"));
    char *base_c = eval_sv_to_cstr_temp(ctx, base);
    EVAL_OOM_RETURN_IF_NULL(ctx, base_c, nob_sv_from_cstr(""));
    (void)mkdir_p_local(ctx, base_c);

    String_View name = sv_copy_to_arena(eval_temp_arena(ctx),
                                        nob_sv_from_cstr(nob_temp_sprintf("try_compile_%zu", s_scratch_counter)));
    return eval_sv_path_join(eval_temp_arena(ctx), base, name);
}

static String_View try_compile_resolve_in_dir(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    if (path.count == 0) return path;
    if (eval_sv_is_abs_path(path)) return path;
    return eval_sv_path_join(eval_temp_arena(ctx), base_dir, path);
}

static bool try_compile_append_file_to_log(Evaluator_Context *ctx,
                                           const char *path,
                                           Nob_String_Builder *log) {
    if (!ctx || !path || !log) return false;
    if (nob_file_exists(path) == 0) return true;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return true;
    if (sb.count > 0) {
        nob_sb_append_buf(log, sb.items, sb.count);
        if (log->count > 0 && log->items[log->count - 1] != '\n') nob_sb_append(log, '\n');
    }
    nob_sb_free(sb);
    return true;
}

static bool try_compile_run_command_captured(Evaluator_Context *ctx,
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

static String_View try_compile_finish_log(Evaluator_Context *ctx, Nob_String_Builder *log) {
    if (!ctx || !log || log->count == 0) return nob_sv_from_cstr("");
    String_View out = sv_copy_to_arena(eval_temp_arena(ctx), nob_sv_from_parts(log->items, log->count));
    nob_sb_free(*log);
    *log = (Nob_String_Builder){0};
    return out;
}

static bool try_compile_add_source_item(Evaluator_Context *ctx,
                                        Try_Compile_Request *req,
                                        String_View path,
                                        Try_Compile_Language forced_lang) {
    if (!ctx || !req || path.count == 0) return false;
    Try_Compile_Source_Item item = {0};
    item.path = path;
    item.language = forced_lang != TRY_COMPILE_LANG_AUTO ? forced_lang : try_compile_detect_language(path);
    return try_compile_source_push(ctx, &req->source_items, item);
}

static bool try_compile_write_generated_source(Evaluator_Context *ctx,
                                               Try_Compile_Request *req,
                                               String_View name,
                                               String_View content,
                                               Try_Compile_Language forced_lang) {
    if (!ctx || !req) return false;
    if (name.count == 0) {
        if (forced_lang == TRY_COMPILE_LANG_CXX) name = nob_sv_from_cstr("try_compile_source.cpp");
        else name = nob_sv_from_cstr("try_compile_source.c");
    }

    String_View src_path = eval_sv_path_join(eval_temp_arena(ctx), req->binary_dir, name);
    char *src_c = eval_sv_to_cstr_temp(ctx, src_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    if (!nob_write_entire_file(src_c, content.data ? content.data : "", content.count)) {
        return false;
    }
    return try_compile_add_source_item(ctx, req, src_path, forced_lang);
}

static bool try_compile_collect_until_keyword(Evaluator_Context *ctx,
                                              const SV_List *args,
                                              size_t *io_index,
                                              SV_List *out) {
    if (!ctx || !args || !io_index || !out) return false;
    while (*io_index + 1 < args->count && !try_compile_is_keyword(args->items[*io_index + 1])) {
        (*io_index)++;
        if (!try_compile_sv_push(ctx, out, args->items[*io_index])) return false;
    }
    return true;
}

static bool try_compile_parse_cmake_flag_define_token(String_View def, String_View *out_key, String_View *out_value) {
    if (out_key) *out_key = nob_sv_from_cstr("");
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (!(def.count > 2 && def.data[0] == '-' && def.data[1] == 'D')) return false;

    String_View kv = nob_sv_from_parts(def.data + 2, def.count - 2);
    if (kv.count == 0) return false;

    const char *eq = memchr(kv.data, '=', kv.count);
    String_View raw_key = kv;
    String_View value = nob_sv_from_cstr("1");
    if (eq) {
        size_t klen = (size_t)(eq - kv.data);
        raw_key = nob_sv_from_parts(kv.data, klen);
        value = nob_sv_from_parts(eq + 1, kv.count - klen - 1);
    }

    const char *colon = memchr(raw_key.data, ':', raw_key.count);
    String_View key = raw_key;
    if (colon) key = nob_sv_from_parts(raw_key.data, (size_t)(colon - raw_key.data));
    if (key.count == 0) return false;

    if (out_key) *out_key = key;
    if (out_value) *out_value = value;
    return true;
}

static bool try_compile_cache_upsert(Evaluator_Context *ctx, String_View key, String_View value) {
    if (!ctx) return false;
    Eval_Cache_Entry *entry = NULL;
    if (ctx->cache_entries) entry = stbds_shgetp_null(ctx->cache_entries, nob_temp_sv_to_cstr(key));
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        entry->value.type = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("INTERNAL"));
        entry->value.doc = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("try_compile result"));
        return !eval_should_stop(ctx);
    }

    char *stable_key = (char*)arena_alloc(eval_event_arena(ctx), key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);
    if (key.count > 0) memcpy(stable_key, key.data, key.count);
    stable_key[key.count] = '\0';

    Eval_Cache_Value cv = {0};
    cv.data = sv_copy_to_event_arena(ctx, value);
    cv.type = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("INTERNAL"));
    cv.doc = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("try_compile result"));
    if (eval_should_stop(ctx)) return false;

    Eval_Cache_Entry *entries = ctx->cache_entries;
    stbds_shput(entries, stable_key, cv);
    ctx->cache_entries = entries;
    return true;
}

static bool try_compile_publish_result(Evaluator_Context *ctx,
                                       Cmake_Event_Origin origin,
                                       const Try_Compile_Request *req,
                                       String_View result,
                                       String_View output_text) {
    if (!ctx || !req) return false;
    if (req->no_cache) {
        if (!eval_var_set(ctx, req->result_var, result)) return false;
    } else {
        if (!try_compile_cache_upsert(ctx, req->result_var, result)) return false;
        Cmake_Event ev = {0};
        ev.kind = EV_SET_CACHE_ENTRY;
        ev.origin = origin;
        ev.as.cache_entry.key = sv_copy_to_event_arena(ctx, req->result_var);
        ev.as.cache_entry.value = sv_copy_to_event_arena(ctx, result);
        if (!emit_event(ctx, ev)) return false;
    }
    if (req->output_var.count > 0) {
        if (!eval_var_set(ctx, req->output_var, output_text)) return false;
    }
    return true;
}

static bool try_compile_append_define_arg(Evaluator_Context *ctx, Nob_Cmd *cmd, String_View def, bool msvc) {
    if (!ctx || !cmd) return false;
    if (def.count == 0) return true;
    if (nob_sv_starts_with(def, nob_sv_from_cstr("-D")) || nob_sv_starts_with(def, nob_sv_from_cstr("/D"))) {
        def = nob_sv_from_parts(def.data + 2, def.count - 2);
    }
    const char *prefix = msvc ? "/D" : "-D";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, def));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_include_arg(Evaluator_Context *ctx, Nob_Cmd *cmd, String_View dir, bool msvc) {
    if (!ctx || !cmd) return false;
    if (dir.count == 0) return true;
    String_View resolved = try_compile_resolve_in_dir(ctx, dir, try_compile_current_src_dir(ctx));
    const char *prefix = msvc ? "/I" : "-I";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, resolved));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_link_dir_arg(Evaluator_Context *ctx, Nob_Cmd *cmd, String_View dir, bool msvc) {
    if (!ctx || !cmd) return false;
    if (dir.count == 0) return true;
    String_View resolved = try_compile_resolve_in_dir(ctx, dir, try_compile_current_src_dir(ctx));
    const char *prefix = msvc ? "/LIBPATH:" : "-L";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, resolved));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_tokenized_flags(Evaluator_Context *ctx,
                                               Nob_Cmd *cmd,
                                               String_View raw_flags) {
    if (!ctx || !cmd) return false;
    if (raw_flags.count == 0) return true;
    size_t i = 0;
    while (i < raw_flags.count) {
        while (i < raw_flags.count && isspace((unsigned char)raw_flags.data[i])) i++;
        size_t start = i;
        while (i < raw_flags.count && !isspace((unsigned char)raw_flags.data[i])) i++;
        if (i > start) {
            char *arg = eval_sv_to_cstr_temp(ctx, nob_sv_from_parts(raw_flags.data + start, i - start));
            EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
            nob_cmd_append(cmd, arg);
        }
    }
    return true;
}

static bool try_compile_append_required_compile_settings(Evaluator_Context *ctx,
                                                         Nob_Cmd *cmd,
                                                         const Try_Compile_Request *req,
                                                         Try_Compile_Language lang,
                                                         bool msvc) {
    if (!ctx || !cmd || !req) return false;

    for (size_t i = 0; i < req->compile_definitions.count; i++) {
        if (!try_compile_append_define_arg(ctx, cmd, req->compile_definitions.items[i], msvc)) return false;
    }

    String_View required_defs = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_DEFINITIONS"));
    if (required_defs.count > 0) {
        SV_List parts = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_defs, &parts)) return false;
        for (size_t i = 0; i < parts.count; i++) {
            if (!try_compile_append_define_arg(ctx, cmd, parts.items[i], msvc)) return false;
        }
    }

    String_View required_includes = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_INCLUDES"));
    if (required_includes.count > 0) {
        SV_List incs = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_includes, &incs)) return false;
        for (size_t i = 0; i < incs.count; i++) {
            if (!try_compile_append_include_arg(ctx, cmd, incs.items[i], msvc)) return false;
        }
    }

    if (!try_compile_append_tokenized_flags(ctx, cmd, eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_FLAGS")))) {
        return false;
    }

    const Try_Compile_Lang_Props *props = (lang == TRY_COMPILE_LANG_CXX) ? &req->cxx_lang : &req->c_lang;
    if (props->has_value && props->standard.count > 0 && !msvc) {
        bool extensions = !props->extensions_set || props->extensions;
        const char *prefix = (lang == TRY_COMPILE_LANG_CXX)
            ? (extensions ? "gnu++" : "c++")
            : (extensions ? "gnu" : "c");
        String_View std_flag = nob_sv_from_cstr("");
        if (nob_sv_starts_with(props->standard, nob_sv_from_cstr("gnu")) ||
            nob_sv_starts_with(props->standard, nob_sv_from_cstr("c"))) {
            std_flag = try_compile_concat_prefix_temp(ctx, "-std=", props->standard);
        } else {
            std_flag = try_compile_concat_prefix_temp(
                ctx,
                "-std=",
                try_compile_concat_prefix_temp(ctx, prefix, props->standard));
        }
        char *std_c = eval_sv_to_cstr_temp(ctx, std_flag);
        EVAL_OOM_RETURN_IF_NULL(ctx, std_c, false);
        nob_cmd_append(cmd, std_c);
    }

    return true;
}

static bool try_compile_append_link_library_arg(Evaluator_Context *ctx,
                                                Nob_Cmd *cmd,
                                                String_View lib,
                                                const Try_Compile_Target_Artifact *artifacts,
                                                size_t artifact_count,
                                                bool msvc) {
    if (!ctx || !cmd) return false;
    if (lib.count == 0) return true;
    for (size_t i = 0; i < artifact_count; i++) {
        if (nob_sv_eq(artifacts[i].key, lib)) {
            char *artifact_c = eval_sv_to_cstr_temp(ctx, artifacts[i].value);
            EVAL_OOM_RETURN_IF_NULL(ctx, artifact_c, false);
            nob_cmd_append(cmd, artifact_c);
            return true;
        }
    }

    if (msvc) {
        char *arg = eval_sv_to_cstr_temp(ctx, lib);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
        return true;
    }

    if (nob_sv_starts_with(lib, nob_sv_from_cstr("-l")) ||
        nob_sv_starts_with(lib, nob_sv_from_cstr("-L")) ||
        eval_sv_is_abs_path(lib) ||
        nob_sv_end_with(lib, ".a") ||
        nob_sv_end_with(lib, ".so") ||
        nob_sv_end_with(lib, ".dylib") ||
        nob_sv_end_with(lib, ".o")) {
        char *arg = eval_sv_to_cstr_temp(ctx, lib);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
        return true;
    }

    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, "-l", lib));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_required_link_settings(Evaluator_Context *ctx,
                                                      Nob_Cmd *cmd,
                                                      const Try_Compile_Request *req,
                                                      const Try_Compile_Target_Artifact *artifacts,
                                                      size_t artifact_count,
                                                      bool msvc) {
    if (!ctx || !cmd || !req) return false;

    for (size_t i = 0; i < req->link_options.count; i++) {
        char *arg = eval_sv_to_cstr_temp(ctx, req->link_options.items[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
    }

    String_View required_link_options = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LINK_OPTIONS"));
    if (required_link_options.count > 0) {
        SV_List opts = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_link_options, &opts)) return false;
        for (size_t i = 0; i < opts.count; i++) {
            char *arg = eval_sv_to_cstr_temp(ctx, opts.items[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
            nob_cmd_append(cmd, arg);
        }
    }

    String_View required_link_dirs = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LINK_DIRECTORIES"));
    if (required_link_dirs.count > 0) {
        SV_List dirs = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_link_dirs, &dirs)) return false;
        for (size_t i = 0; i < dirs.count; i++) {
            if (!try_compile_append_link_dir_arg(ctx, cmd, dirs.items[i], msvc)) return false;
        }
    }

    for (size_t i = 0; i < req->link_libraries.count; i++) {
        if (!try_compile_append_link_library_arg(ctx, cmd, req->link_libraries.items[i], artifacts, artifact_count, msvc)) {
            return false;
        }
    }

    String_View required_libs = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LIBRARIES"));
    if (required_libs.count > 0) {
        SV_List libs = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_libs, &libs)) return false;
        for (size_t i = 0; i < libs.count; i++) {
            if (!try_compile_append_link_library_arg(ctx, cmd, libs.items[i], artifacts, artifact_count, msvc)) return false;
        }
    }

    return true;
}

static bool try_compile_materialize_output_path(Evaluator_Context *ctx,
                                                String_View bindir,
                                                String_View base_name,
                                                const char *ext,
                                                String_View *out_path) {
    if (!ctx || !out_path) return false;
    String_View name = base_name.count > 0 ? base_name : nob_sv_from_cstr("cmk2nob_try_compile");
    if (ext && ext[0] != '\0') {
        name = svu_concat_suffix_temp(ctx, name, ext);
    }
    *out_path = eval_sv_path_join(eval_temp_arena(ctx), bindir, name);
    return true;
}

static bool try_compile_execute_source_request(Evaluator_Context *ctx,
                                               const Try_Compile_Request *req,
                                               Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};

    Nob_String_Builder log = {0};
    SV_List object_paths = {0};
    size_t compile_units = 0;
    bool any_cxx = false;
    bool any_c = false;
    bool msvc = false;

    for (size_t i = 0; i < req->source_items.count; i++) {
        Try_Compile_Source_Item item = req->source_items.items[i];
        String_View src = item.path;
        if (!eval_sv_is_abs_path(src)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), req->current_src_dir, src);
        }
        Try_Compile_Language lang = item.language != TRY_COMPILE_LANG_AUTO
            ? item.language
            : try_compile_detect_language(src);

        if (lang == TRY_COMPILE_LANG_HEADERS) continue;
        if (lang == TRY_COMPILE_LANG_AUTO) {
            nob_sb_append_cstr(&log, "try_compile failed: unsupported source language\n");
            out_res->output = try_compile_finish_log(ctx, &log);
            return true;
        }
        if (!file_exists_sv(ctx, src)) {
            nob_sb_append_cstr(&log, "try_compile source file not found: ");
            nob_sb_append_buf(&log, src.data, src.count);
            nob_sb_append(&log, '\n');
            out_res->output = try_compile_finish_log(ctx, &log);
            return true;
        }

        compile_units++;
        any_cxx = any_cxx || (lang == TRY_COMPILE_LANG_CXX);
        any_c = any_c || (lang == TRY_COMPILE_LANG_C);

        String_View obj_name = sv_copy_to_arena(eval_temp_arena(ctx),
                                                nob_sv_from_cstr(nob_temp_sprintf("obj_%zu%s",
                                                                                 i,
                                                                                 msvc ? ".obj" : ".o")));
        String_View obj_path = eval_sv_path_join(eval_temp_arena(ctx), req->binary_dir, obj_name);
        char *compiler_c = eval_sv_to_cstr_temp(
            ctx,
            lang == TRY_COMPILE_LANG_CXX
                ? eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"))
                : eval_var_get(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER")));
        char *src_c = eval_sv_to_cstr_temp(ctx, src);
        char *obj_c = eval_sv_to_cstr_temp(ctx, obj_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, compiler_c, false);
        EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
        EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, compiler_c);
#if defined(_WIN32)
        msvc = eval_sv_eq_ci_lit(eval_var_get(ctx, nob_sv_from_cstr("MSVC")), "1");
        if (msvc) {
            String_View fo_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                                  nob_sv_from_cstr(nob_temp_sprintf("/Fo:%s", obj_c)));
            nob_cmd_append(&cmd, "/nologo", "/c", src_c, fo_arg.data);
        } else
#endif
        {
            nob_cmd_append(&cmd, "-c", src_c, "-o", obj_c);
        }

        if (!try_compile_append_required_compile_settings(ctx, &cmd, req, lang, msvc)) {
            nob_cmd_free(cmd);
            nob_sb_free(log);
            return false;
        }

        bool cmd_ok = false;
        if (!try_compile_run_command_captured(ctx, &cmd, req->binary_dir, &log, &cmd_ok)) {
            nob_sb_free(log);
            return false;
        }
        if (!cmd_ok) {
            out_res->output = try_compile_finish_log(ctx, &log);
            out_res->ok = false;
            return true;
        }

        if (!try_compile_sv_push(ctx, &object_paths, obj_path)) {
            nob_sb_free(log);
            return false;
        }
    }

    if (compile_units == 0) {
        nob_sb_append_cstr(&log, "try_compile failed: no compilable source units\n");
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->ok = false;
        return true;
    }

    String_View target_type = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_TARGET_TYPE"));
    Try_Compile_Build_Kind build_kind = req->signature == TRY_COMPILE_SIGNATURE_SOURCE
        ? (eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY")
            ? TRY_COMPILE_BUILD_STATIC_LIBRARY
            : TRY_COMPILE_BUILD_EXECUTABLE)
        : TRY_COMPILE_BUILD_EXECUTABLE;

    String_View output_path = nob_sv_from_cstr("");
    if (build_kind == TRY_COMPILE_BUILD_STATIC_LIBRARY) {
#if defined(_WIN32)
        if (!try_compile_materialize_output_path(ctx, req->binary_dir, nob_sv_from_cstr("cmk2nob_try_compile"), ".lib", &output_path)) {
            nob_sb_free(log);
            return false;
        }
        char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        Nob_Cmd cmd = {0};
        String_View out_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                               nob_sv_from_cstr(nob_temp_sprintf("/OUT:%s", out_c)));
        nob_cmd_append(&cmd, "lib.exe", "/NOLOGO", out_arg.data);
        for (size_t i = 0; i < object_paths.count; i++) {
            char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths.items[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
            nob_cmd_append(&cmd, obj_c);
        }
#else
        if (!try_compile_materialize_output_path(ctx, req->binary_dir, nob_sv_from_cstr("libcmk2nob_try_compile"), ".a", &output_path)) {
            nob_sb_free(log);
            return false;
        }
        char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "ar", "rcs", out_c);
        for (size_t i = 0; i < object_paths.count; i++) {
            char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths.items[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
            nob_cmd_append(&cmd, obj_c);
        }
#endif
        bool cmd_ok = false;
        if (!try_compile_run_command_captured(ctx, &cmd, req->binary_dir, &log, &cmd_ok)) {
            nob_sb_free(log);
            return false;
        }
        out_res->ok = cmd_ok;
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->artifact_path = cmd_ok ? output_path : nob_sv_from_cstr("");
        return true;
    }

#if defined(_WIN32)
    msvc = eval_sv_eq_ci_lit(eval_var_get(ctx, nob_sv_from_cstr("MSVC")), "1");
#endif
    Try_Compile_Language link_lang = TRY_COMPILE_LANG_C;
    if (req->linker_language.count > 0) {
        if (eval_sv_eq_ci_lit(req->linker_language, "CXX")) link_lang = TRY_COMPILE_LANG_CXX;
        else link_lang = TRY_COMPILE_LANG_C;
    } else if (any_cxx) {
        link_lang = TRY_COMPILE_LANG_CXX;
    } else if (any_c) {
        link_lang = TRY_COMPILE_LANG_C;
    }

    if (!try_compile_materialize_output_path(ctx,
                                             req->binary_dir,
                                             nob_sv_from_cstr("cmk2nob_try_compile"),
#if defined(_WIN32)
                                             ".exe",
#else
                                             "",
#endif
                                             &output_path)) {
        nob_sb_free(log);
        return false;
    }

    char *linker_c = eval_sv_to_cstr_temp(
        ctx,
        link_lang == TRY_COMPILE_LANG_CXX
            ? eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"))
            : eval_var_get(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER")));
    char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, linker_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);

    Nob_Cmd link_cmd = {0};
    nob_cmd_append(&link_cmd, linker_c);
#if defined(_WIN32)
    if (msvc) {
        String_View fe_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                              nob_sv_from_cstr(nob_temp_sprintf("/Fe:%s", out_c)));
        nob_cmd_append(&link_cmd, "/nologo", fe_arg.data);
    } else
#endif
    {
        nob_cmd_append(&link_cmd, "-o", out_c);
    }
    for (size_t i = 0; i < object_paths.count; i++) {
        char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths.items[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
        nob_cmd_append(&link_cmd, obj_c);
    }
    if (!try_compile_append_required_link_settings(ctx, &link_cmd, req, NULL, 0, msvc)) {
        nob_cmd_free(link_cmd);
        nob_sb_free(log);
        return false;
    }

    bool link_ok = false;
    if (!try_compile_run_command_captured(ctx, &link_cmd, req->binary_dir, &log, &link_ok)) {
        nob_sb_free(log);
        return false;
    }

    out_res->ok = link_ok;
    out_res->artifact_path = link_ok ? output_path : nob_sv_from_cstr("");
    out_res->output = try_compile_finish_log(ctx, &log);
    return true;
}

static bool try_compile_resolve_project_target_sources(Evaluator_Context *ctx,
                                                       String_View source_dir,
                                                       String_View binary_dir,
                                                       const String_List *raw_sources,
                                                       Try_Compile_Source_List *out_sources) {
    if (!ctx || !raw_sources || !out_sources) return false;
    for (size_t i = 0; i < raw_sources->count; i++) {
        String_View src = raw_sources->items[i];
        if (!eval_sv_is_abs_path(src)) {
            String_View src_try = eval_sv_path_join(eval_temp_arena(ctx), source_dir, src);
            if (file_exists_sv(ctx, src_try)) src = src_try;
            else src = eval_sv_path_join(eval_temp_arena(ctx), binary_dir, src);
        }
        Try_Compile_Source_Item item = {0};
        item.path = src;
        item.language = try_compile_detect_language(src);
        if (!try_compile_source_push(ctx, out_sources, item)) return false;
    }
    return true;
}

static bool try_compile_build_target(Evaluator_Context *ctx,
                                     const Try_Compile_Request *base_req,
                                     Build_Target *target,
                                     const Build_Model *model,
                                     const Try_Compile_Target_Artifact *artifacts,
                                     size_t artifact_count,
                                     Try_Compile_Execution_Result *out_res) {
    if (!ctx || !base_req || !target || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};

    Try_Compile_Request req = *base_req;
    req.source_items = (Try_Compile_Source_List){0};
    req.compile_definitions = (SV_List){0};
    req.link_options = (SV_List){0};
    req.link_libraries = (SV_List){0};
    req.linker_language = base_req->linker_language;

    const String_List *global_defs = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_DEFINITIONS);
    const String_List *global_link_opts = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_LINK_OPTIONS);
    const String_List *global_link_libs = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_LINK_LIBRARIES);

    for (size_t i = 0; i < global_defs->count; i++) {
        if (!try_compile_sv_push(ctx, &req.compile_definitions, global_defs->items[i])) return false;
    }
    for (size_t i = 0; i < global_link_opts->count; i++) {
        if (!try_compile_sv_push(ctx, &req.link_options, global_link_opts->items[i])) return false;
    }
    for (size_t i = 0; i < global_link_libs->count; i++) {
        if (!try_compile_sv_push(ctx, &req.link_libraries, global_link_libs->items[i])) return false;
    }

    String_List local = {0};
    string_list_init(&local);
    build_target_collect_effective_compile_definitions(target, eval_temp_arena(ctx), NULL, &local);
    for (size_t i = 0; i < local.count; i++) {
        if (!try_compile_sv_push(ctx, &req.compile_definitions, local.items[i])) return false;
    }

    local = (String_List){0};
    string_list_init(&local);
    build_target_collect_effective_link_options(target, eval_temp_arena(ctx), NULL, &local);
    for (size_t i = 0; i < local.count; i++) {
        if (!try_compile_sv_push(ctx, &req.link_options, local.items[i])) return false;
    }

    local = (String_List){0};
    string_list_init(&local);
    build_target_collect_effective_link_libraries(target, eval_temp_arena(ctx), NULL, &local);
    for (size_t i = 0; i < local.count; i++) {
        if (!try_compile_sv_push(ctx, &req.link_libraries, local.items[i])) return false;
    }

    const String_List *target_sources = build_target_get_string_list(target, BUILD_TARGET_LIST_SOURCES);
    if (!try_compile_resolve_project_target_sources(ctx, base_req->source_dir, base_req->binary_dir, target_sources, &req.source_items)) {
        return false;
    }

    Target_Type target_type = build_target_get_type(target);
    if (target_type == TARGET_OBJECT_LIB) {
        req.linker_language = base_req->linker_language;
    }
    if (!try_compile_execute_source_request(ctx, &req, out_res)) return false;
    if (!out_res->ok || target_type == TARGET_EXECUTABLE) return true;

    if (target_type == TARGET_STATIC_LIB) return true;
    if (target_type == TARGET_SHARED_LIB) return true;
    if (target_type == TARGET_OBJECT_LIB) return true;

    (void)artifacts;
    (void)artifact_count;
    return true;
}

static bool try_compile_execute_project_request(Evaluator_Context *ctx,
                                                const Node *node,
                                                const Try_Compile_Request *req,
                                                Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    (void)node;
    *out_res = (Try_Compile_Execution_Result){0};

    String_View cmakelists = eval_sv_path_join(eval_temp_arena(ctx), req->source_dir, nob_sv_from_cstr("CMakeLists.txt"));
    if (!file_exists_sv(ctx, cmakelists)) {
        out_res->output = try_compile_concat_sv_temp(
            ctx,
            nob_sv_from_cstr("try_compile project source directory missing CMakeLists.txt: "),
            req->source_dir);
        out_res->ok = false;
        return true;
    }

    Arena *child_temp = arena_create(2 * 1024 * 1024);
    Arena *child_event = arena_create(4 * 1024 * 1024);
    if (!child_temp || !child_event) {
        if (child_temp) arena_destroy(child_temp);
        if (child_event) arena_destroy(child_event);
        return ctx_oom(ctx);
    }

    Cmake_Event_Stream *child_stream = event_stream_create(child_event);
    if (!child_stream) {
        arena_destroy(child_temp);
        arena_destroy(child_event);
        return ctx_oom(ctx);
    }

    Evaluator_Init init = {0};
    init.arena = child_temp;
    init.event_arena = child_event;
    init.stream = child_stream;
    init.source_dir = req->source_dir;
    init.binary_dir = req->binary_dir;
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *child = evaluator_create(&init);
    if (!child) {
        arena_destroy(child_temp);
        arena_destroy(child_event);
        return ctx_oom(ctx);
    }

    static const char *k_forward_vars[] = {
        "CMAKE_C_COMPILER",
        "CMAKE_CXX_COMPILER",
        "CMAKE_C_COMPILER_ID",
        "CMAKE_CXX_COMPILER_ID",
        "CMAKE_SYSTEM_NAME",
        "CMAKE_HOST_SYSTEM_NAME",
        "CMAKE_SYSTEM_PROCESSOR",
        "CMAKE_REQUIRED_DEFINITIONS",
        "CMAKE_REQUIRED_FLAGS",
        "CMAKE_REQUIRED_INCLUDES",
        "CMAKE_REQUIRED_LINK_OPTIONS",
        "CMAKE_REQUIRED_LINK_DIRECTORIES",
        "CMAKE_REQUIRED_LIBRARIES",
        "CMAKE_TRY_COMPILE_TARGET_TYPE",
        "CMAKE_TRY_COMPILE_CONFIGURATION",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_forward_vars); i++) {
        String_View key = nob_sv_from_cstr(k_forward_vars[i]);
        String_View value = eval_var_get(ctx, key);
        if (value.count > 0 && !eval_var_set(child, key, value)) {
            evaluator_destroy(child);
            arena_destroy(child_temp);
            arena_destroy(child_event);
            return false;
        }
    }

    String_View platform_vars = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_PLATFORM_VARIABLES"));
    if (platform_vars.count > 0) {
        SV_List names = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), platform_vars, &names)) {
            evaluator_destroy(child);
            arena_destroy(child_temp);
            arena_destroy(child_event);
            return false;
        }
        for (size_t i = 0; i < names.count; i++) {
            String_View value = eval_var_get(ctx, names.items[i]);
            if (value.count > 0 && !eval_var_set(child, names.items[i], value)) {
                evaluator_destroy(child);
                arena_destroy(child_temp);
                arena_destroy(child_event);
                return false;
            }
        }
    }

    for (size_t i = 0; i < req->cmake_flags.count; i++) {
        String_View key = nob_sv_from_cstr("");
        String_View value = nob_sv_from_cstr("");
        if (!try_compile_parse_cmake_flag_define_token(req->cmake_flags.items[i], &key, &value)) continue;
        if (!eval_var_set(child, key, value)) {
            evaluator_destroy(child);
            arena_destroy(child_temp);
            arena_destroy(child_event);
            return false;
        }
    }

    bool child_ok = eval_execute_file(child, cmakelists, false, req->binary_dir);
    if (!child_ok || eval_should_stop(child)) {
        out_res->ok = false;
        out_res->output = nob_sv_from_cstr("try_compile(PROJECT) failed while evaluating the child project");
        evaluator_destroy(child);
        arena_destroy(child_temp);
        arena_destroy(child_event);
        return true;
    }

    Build_Model_Builder *builder = builder_create(child_event, NULL);
    if (!builder || !builder_apply_stream(builder, child_stream)) {
        out_res->ok = false;
        out_res->output = nob_sv_from_cstr("try_compile(PROJECT) failed while building the child model");
        evaluator_destroy(child);
        arena_destroy(child_temp);
        arena_destroy(child_event);
        return true;
    }

    Build_Model *model = builder_finish(builder);
    if (!model) {
        out_res->ok = false;
        out_res->output = nob_sv_from_cstr("try_compile(PROJECT) failed while finalizing the child model");
        evaluator_destroy(child);
        arena_destroy(child_temp);
        arena_destroy(child_event);
        return true;
    }

    size_t target_count = build_model_get_target_count(model);
    Try_Compile_Target_Artifact *artifacts = NULL;
    size_t artifact_count = 0;
    if (target_count > 0) {
        artifacts = (Try_Compile_Target_Artifact*)arena_alloc(eval_temp_arena(ctx), target_count * sizeof(*artifacts));
        EVAL_OOM_RETURN_IF_NULL(ctx, artifacts, false);
    }

    bool any_selected = false;
    bool all_ok = true;
    Nob_String_Builder combined_log = {0};
    for (size_t i = 0; i < target_count; i++) {
        Build_Target *target = build_model_get_target_at(model, i);
        if (!target) continue;
        Target_Type type = build_target_get_type(target);
        if (type == TARGET_IMPORTED || type == TARGET_ALIAS || type == TARGET_INTERFACE_LIB || type == TARGET_UTILITY) {
            continue;
        }
        if (req->target_name.count > 0 && !nob_sv_eq(build_target_get_name(target), req->target_name)) continue;
        if (req->target_name.count == 0 && build_target_is_exclude_from_all(target)) continue;

        any_selected = true;
        Try_Compile_Execution_Result one = {0};
        if (!try_compile_build_target(ctx, req, target, model, artifacts, artifact_count, &one)) {
            evaluator_destroy(child);
            arena_destroy(child_temp);
            arena_destroy(child_event);
            nob_sb_free(combined_log);
            return false;
        }
        if (one.output.count > 0) {
            nob_sb_append_buf(&combined_log, one.output.data, one.output.count);
            if (combined_log.count > 0 && combined_log.items[combined_log.count - 1] != '\n') nob_sb_append(&combined_log, '\n');
        }
        if (!one.ok) all_ok = false;
        if (one.ok && one.artifact_path.count > 0 && artifact_count < target_count) {
            artifacts[artifact_count].key = build_target_get_name(target);
            artifacts[artifact_count].value = one.artifact_path;
            artifact_count++;
        }
        if (!one.ok && req->target_name.count > 0) break;
    }

    if (!any_selected) {
        out_res->ok = true;
        out_res->output = nob_sv_from_cstr("");
    } else {
        out_res->ok = all_ok;
        out_res->output = try_compile_finish_log(ctx, &combined_log);
    }

    if (!out_res->ok && out_res->output.count == 0) {
        out_res->output = nob_sv_from_cstr("try_compile(PROJECT) failed to build the requested targets");
    }

    evaluator_destroy(child);
    arena_destroy(child_temp);
    arena_destroy(child_event);
    return true;
}

static bool try_compile_parse_source_options(Evaluator_Context *ctx,
                                             const Node *node,
                                             const SV_List *args,
                                             size_t start,
                                             Try_Compile_Request *req) {
    if (!ctx || !node || !args || !req) return false;
    Try_Compile_Language current_type = TRY_COMPILE_LANG_AUTO;

    for (size_t i = start; i < args->count; i++) {
        String_View tok = args->items[i];

        if (eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE")) {
            if (i + 1 < args->count) req->output_var = args->items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COPY_FILE")) {
            if (i + 1 < args->count) req->copy_file_path = args->items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COPY_FILE_ERROR")) {
            if (i + 1 < args->count) req->copy_file_error_var = args->items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION")) {
            if (i + 1 < args->count) req->log_description = args->items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINKER_LANGUAGE")) {
            if (i + 1 < args->count) req->linker_language = args->items[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "NO_CACHE")) {
            req->no_cache = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCES_TYPE")) {
            if (i + 1 < args->count) current_type = try_compile_language_from_sources_type(args->items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_STANDARD")) {
            if (i + 1 < args->count) {
                req->c_lang.has_value = true;
                req->c_lang.standard = args->items[++i];
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_STANDARD_REQUIRED")) {
            if (i + 1 < args->count) req->c_lang.standard_required = !try_compile_is_false(args->items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_EXTENSIONS")) {
            if (i + 1 < args->count) {
                req->c_lang.extensions_set = true;
                req->c_lang.extensions = !try_compile_is_false(args->items[++i]);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_STANDARD")) {
            if (i + 1 < args->count) {
                req->cxx_lang.has_value = true;
                req->cxx_lang.standard = args->items[++i];
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_STANDARD_REQUIRED")) {
            if (i + 1 < args->count) req->cxx_lang.standard_required = !try_compile_is_false(args->items[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_EXTENSIONS")) {
            if (i + 1 < args->count) {
                req->cxx_lang.extensions_set = true;
                req->cxx_lang.extensions = !try_compile_is_false(args->items[++i]);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COMPILE_DEFINITIONS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->compile_definitions)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINK_OPTIONS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->link_options)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINK_LIBRARIES")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->link_libraries)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->cmake_flags)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCES")) {
            SV_List values = {0};
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            for (size_t vi = 0; vi < values.count; vi++) {
                if (!try_compile_add_source_item(ctx, req, values.items[vi], current_type)) return false;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_CONTENT")) {
            SV_List values = {0};
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (values.count == 0) continue;
            String_View content = values.count > 1
                ? svu_join_space_temp(ctx, &values.items[1], values.count - 1)
                : nob_sv_from_cstr("");
            if (!try_compile_write_generated_source(ctx, req, values.items[0], content, current_type)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_VAR")) {
            SV_List values = {0};
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (values.count < 2) continue;
            if (!try_compile_write_generated_source(ctx,
                                                    req,
                                                    values.items[0],
                                                    eval_var_get(ctx, values.items[1]),
                                                    current_type)) {
                return false;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_FILE")) {
            SV_List values = {0};
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (values.count < 2) continue;

            String_View src_file = try_compile_resolve_in_dir(ctx, values.items[1], req->current_src_dir);
            char *src_c = eval_sv_to_cstr_temp(ctx, src_file);
            EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);

            Nob_String_Builder sb = {0};
            if (!nob_read_entire_file(src_c, &sb)) continue;
            String_View content = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
            bool ok = try_compile_write_generated_source(ctx, req, values.items[0], content, current_type);
            nob_sb_free(sb);
            if (!ok) return false;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "PROJECT") ||
            eval_sv_eq_ci_lit(tok, "SOURCE_DIR") ||
            eval_sv_eq_ci_lit(tok, "BINARY_DIR") ||
            eval_sv_eq_ci_lit(tok, "TARGET")) {
            return eval_emit_diag(ctx,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("dispatcher"),
                                  node->as.cmd.name,
                                  eval_origin_from_node(ctx, node),
                                  nob_sv_from_cstr("PROJECT-signature keywords are invalid in try_compile(SOURCE ...)"),
                                  nob_sv_from_cstr("Use try_compile(<out-var> PROJECT ... SOURCE_DIR ...)"));
        }

        if (!try_compile_add_source_item(ctx, req, tok, current_type)) return false;
    }

    return true;
}

static bool try_compile_parse_request(Evaluator_Context *ctx,
                                      const Node *node,
                                      const SV_List *args,
                                      Try_Compile_Request *out_req) {
    if (!ctx || !node || !args || !out_req) return false;
    if (args->count < 2) {
        return eval_emit_diag(ctx,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("dispatcher"),
                              node->as.cmd.name,
                              eval_origin_from_node(ctx, node),
                              nob_sv_from_cstr("try_compile() requires at least a result variable"),
                              nob_sv_from_cstr("Usage: try_compile(<out-var> <bindir> <src> ...)"));
    }

    *out_req = (Try_Compile_Request){
        .signature = TRY_COMPILE_SIGNATURE_SOURCE,
        .result_var = args->items[0],
        .current_src_dir = try_compile_current_src_dir(ctx),
        .current_bin_dir = try_compile_current_bin_dir(ctx),
        .binary_dir = nob_sv_from_cstr(""),
    };

    if (eval_sv_eq_ci_lit(args->items[1], "PROJECT")) {
        out_req->signature = TRY_COMPILE_SIGNATURE_PROJECT;
        size_t i = 1;
        for (; i < args->count; i++) {
            String_View tok = args->items[i];
            if (eval_sv_eq_ci_lit(tok, "PROJECT")) {
                if (i + 1 < args->count) out_req->project_name = args->items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "SOURCE_DIR")) {
                if (i + 1 < args->count) out_req->source_dir = try_compile_resolve_in_dir(ctx, args->items[++i], out_req->current_src_dir);
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "BINARY_DIR")) {
                if (i + 1 < args->count) out_req->binary_dir = try_compile_resolve_in_dir(ctx, args->items[++i], out_req->current_bin_dir);
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "TARGET")) {
                if (i + 1 < args->count) out_req->target_name = args->items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE")) {
                if (i + 1 < args->count) out_req->output_var = args->items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION")) {
                if (i + 1 < args->count) out_req->log_description = args->items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "NO_CACHE")) {
                out_req->no_cache = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS")) {
                if (!try_compile_collect_until_keyword(ctx, args, &i, &out_req->cmake_flags)) return false;
                continue;
            }
        }

        if (out_req->source_dir.count == 0) {
            return eval_emit_diag(ctx,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("dispatcher"),
                                  node->as.cmd.name,
                                  eval_origin_from_node(ctx, node),
                                  nob_sv_from_cstr("try_compile(PROJECT ...) requires SOURCE_DIR"),
                                  nob_sv_from_cstr("Usage: try_compile(<out-var> PROJECT <name> SOURCE_DIR <dir> ...)"));
        }

        if (out_req->binary_dir.count == 0) {
            out_req->binary_dir = try_compile_make_scratch_dir(ctx, out_req->current_bin_dir);
        }
        char *bindir_c = eval_sv_to_cstr_temp(ctx, out_req->binary_dir);
        EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
        (void)mkdir_p_local(ctx, bindir_c);
        return true;
    }

    size_t opt_start = 1;
    if (!try_compile_is_keyword(args->items[1])) {
        out_req->binary_dir = try_compile_resolve_in_dir(ctx, args->items[1], out_req->current_bin_dir);
        opt_start = 2;
    } else {
        out_req->binary_dir = try_compile_make_scratch_dir(ctx, out_req->current_bin_dir);
    }

    char *bindir_c = eval_sv_to_cstr_temp(ctx, out_req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
    (void)mkdir_p_local(ctx, bindir_c);

    if (!try_compile_parse_source_options(ctx, node, args, opt_start, out_req)) return false;
    if (out_req->source_items.count == 0) {
        return eval_emit_diag(ctx,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("dispatcher"),
                              node->as.cmd.name,
                              eval_origin_from_node(ctx, node),
                              nob_sv_from_cstr("try_compile(SOURCE ...) requires at least one source input"),
                              nob_sv_from_cstr("Use SOURCES, SOURCE_FROM_CONTENT, SOURCE_FROM_VAR or SOURCE_FROM_FILE"));
    }

    return true;
}

bool eval_handle_try_compile(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return false;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Try_Compile_Request req = {0};
    if (!try_compile_parse_request(ctx, node, &args, &req)) {
        return !eval_should_stop(ctx);
    }

    Try_Compile_Execution_Result exec_res = {0};
    bool ok = req.signature == TRY_COMPILE_SIGNATURE_PROJECT
        ? try_compile_execute_project_request(ctx, node, &req, &exec_res)
        : try_compile_execute_source_request(ctx, &req, &exec_res);
    if (!ok) return !eval_should_stop(ctx);

    if (req.signature == TRY_COMPILE_SIGNATURE_SOURCE && req.copy_file_path.count > 0) {
        String_View target_type = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_TARGET_TYPE"));
        if (eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY")) {
            exec_res.ok = false;
            if (exec_res.output.count == 0) {
                exec_res.output = nob_sv_from_cstr("try_compile COPY_FILE is invalid when CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY");
            }
            if (req.copy_file_error_var.count > 0) {
                (void)eval_var_set(ctx, req.copy_file_error_var, nob_sv_from_cstr("COPY_FILE requires an executable artifact"));
            }
        } else if (exec_res.ok && exec_res.artifact_path.count > 0) {
            String_View dst = try_compile_resolve_in_dir(ctx, req.copy_file_path, req.current_bin_dir);
            String_View parent = svu_dirname(dst);
            char *parent_c = eval_sv_to_cstr_temp(ctx, parent);
            char *src_c = eval_sv_to_cstr_temp(ctx, exec_res.artifact_path);
            char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
            EVAL_OOM_RETURN_IF_NULL(ctx, parent_c, !eval_should_stop(ctx));
            EVAL_OOM_RETURN_IF_NULL(ctx, src_c, !eval_should_stop(ctx));
            EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, !eval_should_stop(ctx));
            (void)mkdir_p_local(ctx, parent_c);
            bool copied = nob_copy_file(src_c, dst_c);
            if (req.copy_file_error_var.count > 0) {
                (void)eval_var_set(ctx,
                                   req.copy_file_error_var,
                                   copied ? nob_sv_from_cstr("") : nob_sv_from_cstr("try_compile COPY_FILE failed"));
            }
            if (!copied) exec_res.ok = false;
        } else if (req.copy_file_error_var.count > 0) {
            (void)eval_var_set(ctx, req.copy_file_error_var, nob_sv_from_cstr("try_compile COPY_FILE failed"));
        }
    }

    String_View result = exec_res.ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
    String_View output_text = exec_res.output.count > 0 ? exec_res.output : nob_sv_from_cstr("");
    if (!try_compile_publish_result(ctx, origin, &req, result, output_text)) {
        return !eval_should_stop(ctx);
    }

    if (req.log_description.count > 0) {
        Nob_String_Builder log = {0};
        nob_sb_append_buf(&log, req.log_description.data, req.log_description.count);
        nob_sb_append_cstr(&log, ": ");
        nob_sb_append_cstr(&log, exec_res.ok ? "success" : "failure");
        if (req.binary_dir.count > 0) {
            nob_sb_append_cstr(&log, " (");
            nob_sb_append_buf(&log, req.binary_dir.data, req.binary_dir.count);
            nob_sb_append(&log, ')');
        }
        (void)eval_append_configure_log(ctx, node, nob_sv_from_parts(log.items, log.count));
        nob_sb_free(log);
    }

    return !eval_should_stop(ctx);
}
