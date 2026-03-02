#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"
#include "stb_ds.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

String_View sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return nob_sv_from_cstr("");
    if (sv.count == 0 || sv.data == NULL) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

char *eval_sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

bool eval_emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

bool eval_sv_key_eq(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool eval_sv_eq_ci_lit(String_View a, const char *lit) {
    String_View b = nob_sv_from_cstr(lit);
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

String_View eval_normalize_compile_definition_item(String_View item) {
    if (item.count >= 2 && item.data && item.data[0] == '-' && (item.data[1] == 'D' || item.data[1] == 'd')) {
        return nob_sv_from_parts(item.data + 2, item.count - 2);
    }
    return item;
}

String_View eval_current_source_dir_for_paths(Evaluator_Context *ctx) {
    String_View cur_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (cur_src.count == 0 && ctx) cur_src = ctx->source_dir;
    return cur_src;
}

String_View eval_property_upper_name_temp(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), name.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < name.count; i++) {
        buf[i] = (char)toupper((unsigned char)name.data[i]);
    }
    buf[name.count] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_property_scope_upper_temp(Evaluator_Context *ctx, String_View raw_scope, String_View *out_scope_upper) {
    (void)ctx;
    if (!out_scope_upper) return false;
    *out_scope_upper = nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(raw_scope, "GLOBAL")) *out_scope_upper = nob_sv_from_cstr("GLOBAL");
    else if (eval_sv_eq_ci_lit(raw_scope, "DIRECTORY")) *out_scope_upper = nob_sv_from_cstr("DIRECTORY");
    else if (eval_sv_eq_ci_lit(raw_scope, "TARGET")) *out_scope_upper = nob_sv_from_cstr("TARGET");
    else if (eval_sv_eq_ci_lit(raw_scope, "SOURCE")) *out_scope_upper = nob_sv_from_cstr("SOURCE");
    else if (eval_sv_eq_ci_lit(raw_scope, "INSTALL")) *out_scope_upper = nob_sv_from_cstr("INSTALL");
    else if (eval_sv_eq_ci_lit(raw_scope, "TEST")) *out_scope_upper = nob_sv_from_cstr("TEST");
    else if (eval_sv_eq_ci_lit(raw_scope, "VARIABLE")) *out_scope_upper = nob_sv_from_cstr("VARIABLE");
    else if (eval_sv_eq_ci_lit(raw_scope, "CACHE")) *out_scope_upper = nob_sv_from_cstr("CACHE");
    else if (eval_sv_eq_ci_lit(raw_scope, "CACHED_VARIABLE")) *out_scope_upper = nob_sv_from_cstr("CACHED_VARIABLE");

    return out_scope_upper->count > 0;
}

String_View eval_property_store_key_temp(Evaluator_Context *ctx,
                                         String_View scope_upper,
                                         String_View object_id,
                                         String_View prop_upper) {
    static const char prefix[] = "NOBIFY_PROPERTY_";
    if (!ctx) return nob_sv_from_cstr("");

    bool has_obj = object_id.count > 0;
    size_t total = (sizeof(prefix) - 1) + scope_upper.count + 2 + prop_upper.count;
    if (has_obj) total += 2 + object_id.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, scope_upper.data, scope_upper.count);
    off += scope_upper.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (has_obj) {
        memcpy(buf + off, object_id.data, object_id.count);
        off += object_id.count;
        buf[off++] = ':';
        buf[off++] = ':';
    }
    memcpy(buf + off, prop_upper.data, prop_upper.count);
    off += prop_upper.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_property_scoped_object_id_temp(Evaluator_Context *ctx,
                                                const char *prefix,
                                                String_View scope_object,
                                                String_View item_object) {
    if (!ctx || !prefix) return nob_sv_from_cstr("");
    String_View pfx = nob_sv_from_cstr(prefix);
    size_t total = pfx.count + 2 + scope_object.count + 2 + item_object.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, pfx.data, pfx.count);
    off += pfx.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (scope_object.count > 0) {
        memcpy(buf + off, scope_object.data, scope_object.count);
        off += scope_object.count;
    }
    buf[off++] = ':';
    buf[off++] = ':';
    if (item_object.count > 0) {
        memcpy(buf + off, item_object.data, item_object.count);
        off += item_object.count;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

const Eval_Property_Definition *eval_property_definition_find(Evaluator_Context *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name) {
    if (!ctx) return NULL;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return NULL;

    for (size_t i = 0; i < ctx->property_definitions.count; i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions.items[i];
        if (!eval_sv_key_eq(def->scope_upper, scope_upper)) continue;
        if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
        return def;
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        String_View cached_scope = nob_sv_from_cstr("CACHED_VARIABLE");
        for (size_t i = 0; i < ctx->property_definitions.count; i++) {
            const Eval_Property_Definition *def = &ctx->property_definitions.items[i];
            if (!eval_sv_key_eq(def->scope_upper, cached_scope)) continue;
            if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
            return def;
        }
    }

    return NULL;
}

static String_View eval_file_parent_dir_view(String_View file_path) {
    if (file_path.count == 0 || !file_path.data) return nob_sv_from_cstr(".");

    size_t end = file_path.count;
    while (end > 0) {
        char c = file_path.data[end - 1];
        if (c != '/' && c != '\\') break;
        end--;
    }
    if (end == 0) return nob_sv_from_cstr("/");

    size_t slash = SIZE_MAX;
    for (size_t i = 0; i < end; i++) {
        char c = file_path.data[i];
        if (c == '/' || c == '\\') slash = i;
    }
    if (slash == SIZE_MAX) return nob_sv_from_cstr(".");
    if (slash == 0) return nob_sv_from_cstr("/");
    if (file_path.data[slash - 1] == ':') {
        return nob_sv_from_parts(file_path.data, slash + 1);
    }
    return nob_sv_from_parts(file_path.data, slash);
}

static bool eval_path_norm_eq_temp(Evaluator_Context *ctx, String_View a, String_View b) {
    String_View an = eval_sv_path_normalize_temp(ctx, a);
    if (eval_should_stop(ctx)) return false;
    String_View bn = eval_sv_path_normalize_temp(ctx, b);
    if (eval_should_stop(ctx)) return false;
    return svu_eq_ci_sv(an, bn);
}

bool eval_test_exists_in_directory_scope(Evaluator_Context *ctx, String_View test_name, String_View scope_dir) {
    if (!ctx || !ctx->stream || test_name.count == 0) return false;
    for (size_t ei = 0; ei < ctx->stream->count; ei++) {
        const Cmake_Event *ev = &ctx->stream->items[ei];
        if (ev->kind != EV_TEST_ADD) continue;
        if (!nob_sv_eq(ev->as.test_add.name, test_name)) continue;
        String_View ev_dir = eval_file_parent_dir_view(ev->origin.file_path);
        if (eval_path_norm_eq_temp(ctx, ev_dir, scope_dir)) return true;
        if (eval_should_stop(ctx)) return false;
    }
    return false;
}

static bool eval_semver_parse_component(String_View sv, int *out_value) {
    if (!out_value || sv.count == 0) return false;
    long long acc = 0;
    for (size_t i = 0; i < sv.count; i++) {
        if (sv.data[i] < '0' || sv.data[i] > '9') return false;
        acc = (acc * 10) + (long long)(sv.data[i] - '0');
        if (acc > INT_MAX) return false;
    }
    *out_value = (int)acc;
    return true;
}

bool eval_semver_parse_strict(String_View version_token, Eval_Semver *out_version) {
    if (!out_version || version_token.count == 0) return false;

    int values[4] = {0, 0, 0, 0};
    size_t value_count = 0;
    size_t pos = 0;
    while (pos < version_token.count) {
        size_t start = pos;
        while (pos < version_token.count && version_token.data[pos] != '.') pos++;
        if (value_count >= 4) return false;
        String_View part = nob_sv_from_parts(version_token.data + start, pos - start);
        if (!eval_semver_parse_component(part, &values[value_count])) return false;
        value_count++;
        if (pos == version_token.count) break;
        pos++;
        if (pos == version_token.count) return false;
    }

    if (value_count < 2 || value_count > 4) return false;
    out_version->major = values[0];
    out_version->minor = values[1];
    out_version->patch = values[2];
    out_version->tweak = values[3];
    return true;
}

int eval_semver_compare(const Eval_Semver *lhs, const Eval_Semver *rhs) {
    if (!lhs || !rhs) return 0;
    if (lhs->major != rhs->major) return (lhs->major < rhs->major) ? -1 : 1;
    if (lhs->minor != rhs->minor) return (lhs->minor < rhs->minor) ? -1 : 1;
    if (lhs->patch != rhs->patch) return (lhs->patch < rhs->patch) ? -1 : 1;
    if (lhs->tweak != rhs->tweak) return (lhs->tweak < rhs->tweak) ? -1 : 1;
    return 0;
}

String_View eval_sv_join_semi_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (!ctx || count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    total += (count - 1);

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (i) buf[off++] = ';';
        if (items[i].count) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out) {
    if (!arena || !out) return false;
    if (input.count == 0) return true;

    size_t start = 0;
    size_t genex_depth = 0;
    for (size_t i = 0; i < input.count; i++) {
        if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
            genex_depth++;
            i++;
            continue;
        }
        if (input.data[i] == '>' && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (input.data[i] == ';' && genex_depth == 0) {
            String_View item = nob_sv_from_parts(input.data + start, i - start);
            if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
                return false;
            }
            out->items[out->count++] = item;
            start = i + 1;
        }
    }

    if (start < input.count) {
        String_View item = nob_sv_from_parts(input.data + start, input.count - start);
        if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
            return false;
        }
        out->items[out->count++] = item;
    }
    return true;
}

bool eval_split_shell_like_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

        size_t off = 0;
        bool touched = false;
        char quote = '\0';
        while (i < input.count) {
            char c = input.data[i];
            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                    touched = true;
                    i++;
                    continue;
                }
                if (c == '\\' && quote == '"' && i + 1 < input.count) {
                    buf[off++] = input.data[i + 1];
                    touched = true;
                    i += 2;
                    continue;
                }
                buf[off++] = c;
                touched = true;
                i++;
                continue;
            }

            if (isspace((unsigned char)c)) break;
            if (c == '"' || c == '\'') {
                quote = c;
                touched = true;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < input.count) {
                buf[off++] = input.data[i + 1];
                touched = true;
                i += 2;
                continue;
            }
            buf[off++] = c;
            touched = true;
            i++;
        }

        buf[off] = '\0';
        if (touched) {
            if (!svu_list_push_temp(ctx, out, nob_sv_from_cstr(buf))) return false;
        }
    }

    return true;
}

bool eval_sv_is_abs_path(String_View p) {
    if (p.count == 0) return false;
    if ((p.count >= 2) &&
        (p.data[0] == '/' || p.data[0] == '\\') &&
        (p.data[1] == '/' || p.data[1] == '\\')) {
        return true; // UNC/network path: //server/share or \\server\share
    }
    if (p.count > 1 && p.data[1] == ':') return true;
    if (p.data[0] == '/' || p.data[0] == '\\') return true;
    return false;
}

String_View eval_sv_path_join(Arena *arena, String_View a, String_View b) {
    if (!arena) return nob_sv_from_cstr("");
    if (a.count == 0) return sv_copy_to_arena(arena, b);
    if (b.count == 0) return sv_copy_to_arena(arena, a);

    bool need_slash = !svu_is_path_sep(a.data[a.count - 1]);
    size_t total = a.count + (need_slash ? 1 : 0) + b.count;

    char *buf = (char*)arena_alloc(arena, total + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t off = 0;
    memcpy(buf + off, a.data, a.count);
    off += a.count;
    if (need_slash) buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';

    return nob_sv_from_cstr(buf);
}

String_View eval_sv_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr(".");

    bool is_unc = input.count >= 2 && svu_is_path_sep(input.data[0]) && svu_is_path_sep(input.data[1]);
    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;

    if (is_unc) {
        pos = 2;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    } else if (has_drive) {
        pos = 2;
        if (pos < input.count && svu_is_path_sep(input.data[pos])) {
            absolute = true;
            while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
        }
    } else if (svu_is_path_sep(input.data[0])) {
        absolute = true;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    }

    SV_List segments = {0};
    size_t unc_root_segments = 0;
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && !svu_is_path_sep(input.data[pos])) pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;

        if (seg.count == 0 || nob_sv_eq(seg, nob_sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, nob_sv_from_cstr(".."))) {
            if (segments.count > 0 &&
                !nob_sv_eq(segments.items[segments.count - 1], nob_sv_from_cstr("..")) &&
                (!is_unc || segments.count > unc_root_segments)) {
                segments.count--;
                continue;
            }
            if (!absolute) {
                if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
            }
            continue;
        }

        if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
        if (is_unc && unc_root_segments < 2) unc_root_segments++;
    }

    size_t total = 0;
    if (is_unc) total += 2;
    else if (has_drive) total += 2;
    if (absolute && !is_unc && !has_drive) total += 1;
    if (absolute && has_drive) total += 1;

    for (size_t i = 0; i < segments.count; i++) {
        if (i > 0 || is_unc || absolute || (has_drive && absolute)) total += 1;
        total += segments.items[i].count;
    }

    if (segments.count == 0) {
        if (is_unc) total += 0;
        else if (has_drive && absolute) {
            if (total == 2) total += 1;
        } else if (!has_drive && !absolute) {
            total += 1;
        }
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (is_unc) {
        buf[off++] = '/';
        buf[off++] = '/';
    } else if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
        if (absolute) buf[off++] = '/';
    } else if (absolute) {
        buf[off++] = '/';
    }

    for (size_t i = 0; i < segments.count; i++) {
        if (off > 0 && buf[off - 1] != '/') buf[off++] = '/';
        memcpy(buf + off, segments.items[i].data, segments.items[i].count);
        off += segments.items[i].count;
    }

    if (segments.count == 0) {
        if (has_drive && absolute) {
            if (off == 2) buf[off++] = '/';
        } else if (!is_unc && !has_drive && !absolute) {
            buf[off++] = '.';
        }
    }

    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_path_resolve_for_cmake_arg(Evaluator_Context *ctx,
                                            String_View raw_path,
                                            String_View base_dir,
                                            bool preserve_generator_expressions) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw_path.count == 0) return nob_sv_from_cstr("");

    if (preserve_generator_expressions &&
        raw_path.count >= 2 &&
        raw_path.data[0] == '$' &&
        raw_path.data[1] == '<') {
        return raw_path;
    }

    String_View resolved = raw_path;
    if (!eval_sv_is_abs_path(resolved)) {
        resolved = eval_sv_path_join(eval_temp_arena(ctx), base_dir, resolved);
    }
    return eval_sv_path_normalize_temp(ctx, resolved);
}

const char *eval_getenv_temp(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return NULL;

#if defined(_WIN32)
    if (!ctx) return NULL;

    size_t name_len = strlen(name);
    char *lookup_name = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup_name, NULL);
    for (size_t i = 0; i < name_len; i++) {
        lookup_name[i] = (char)toupper((unsigned char)name[i]);
    }
    lookup_name[name_len] = '\0';

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    if (needed == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND) return NULL;

        if (err == ERROR_SUCCESS) {
            char *empty = (char*)arena_alloc(eval_temp_arena(ctx), 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, empty, NULL);
            empty[0] = '\0';
            return empty;
        }

        return NULL;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)needed);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);

    DWORD written = GetEnvironmentVariableA(lookup_name, buf, needed);
    if (written >= needed) {
        char *retry = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)written + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, retry, NULL);
        DWORD retry_written = GetEnvironmentVariableA(lookup_name, retry, written + 1);
        if (retry_written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
        return retry;
    }

    if (written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
    return buf;
#else
    (void)ctx;
    return getenv(name);
#endif
}

bool eval_has_env(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return false;

#if defined(_WIN32)
    if (!ctx) return false;
    const char *lookup_name = name;
    size_t name_len = strlen(name);
    char *arena_name = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, arena_name, false);
    for (size_t i = 0; i < name_len; i++) {
        arena_name[i] = (char)toupper((unsigned char)name[i]);
    }
    arena_name[name_len] = '\0';
    lookup_name = arena_name;

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    bool exists = (needed > 0) || (GetLastError() == ERROR_SUCCESS);
    return exists;
#else
    (void)ctx;
    return getenv(name) != NULL;
#endif
}
