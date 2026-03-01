#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"
#include <ctype.h>
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
