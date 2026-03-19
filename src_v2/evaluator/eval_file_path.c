#include "eval_file_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

String_View eval_file_current_src_dir(EvalExecContext *ctx) {
    return eval_current_source_dir(ctx);
}

String_View eval_file_current_bin_dir(EvalExecContext *ctx) {
    return eval_current_binary_dir(ctx);
}

bool eval_file_parse_size_sv(String_View sv, size_t *out) {
    if (!out || sv.count == 0) return false;
    char tmp[64];
    if (sv.count >= sizeof(tmp)) return false;
    memcpy(tmp, sv.data, sv.count);
    tmp[sv.count] = '\0';
    char *end = NULL;
    unsigned long long v = strtoull(tmp, &end, 10);
    if (!end || *end != '\0') return false;
    *out = (size_t)v;
    return true;
}

static bool is_path_safe(String_View path) {
    if (eval_sv_eq_ci_lit(path, "..")) return false;
    for (size_t i = 0; i + 2 < path.count; i++) {
        if (path.data[i] == '.' && path.data[i + 1] == '.' && svu_is_path_sep(path.data[i + 2])) return false;
        if (svu_is_path_sep(path.data[i]) && path.data[i + 1] == '.' && path.data[i + 2] == '.') return false;
    }
    return true;
}

static bool scope_char_eq(char a, char b, bool ci) {
    if (a == '\\') a = '/';
    if (b == '\\') b = '/';
    if (ci) {
        a = (char)tolower((unsigned char)a);
        b = (char)tolower((unsigned char)b);
    }
    return a == b;
}

static bool scope_is_root_like(String_View p) {
    if (p.count == 1 && p.data[0] == '/') return true;
    if (p.count == 3 &&
        isalpha((unsigned char)p.data[0]) &&
        p.data[1] == ':' &&
        p.data[2] == '/') return true;
    return false;
}

static String_View scope_trim_trailing_seps(String_View p) {
    while (p.count > 0 && svu_is_path_sep(p.data[p.count - 1]) && !scope_is_root_like(p)) {
        p.count--;
    }
    return p;
}

static bool scope_path_has_prefix(String_View path, String_View prefix, bool ci) {
    path = scope_trim_trailing_seps(path);
    prefix = scope_trim_trailing_seps(prefix);
    if (prefix.count == 0) return false;
    if (path.count < prefix.count) return false;

    for (size_t i = 0; i < prefix.count; i++) {
        if (!scope_char_eq(path.data[i], prefix.data[i], ci)) return false;
    }

    if (path.count == prefix.count) return true;
    if (prefix.data[prefix.count - 1] == '/' || prefix.data[prefix.count - 1] == '\\') return true;
    return svu_is_path_sep(path.data[prefix.count]);
}

static void scope_normalize_slashes_in_place(char *s) {
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\\') s[i] = '/';
    }
}

static bool scope_canonicalize_existing_cstr_temp(EvalExecContext *ctx,
                                                  const char *path_c,
                                                  char **out_canon_cstr) {
    if (!ctx || !path_c || !out_canon_cstr) return false;
    *out_canon_cstr = NULL;

#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return false;
    }

    DWORD cap = need + 1;
    char *raw = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, raw, false);

    DWORD wrote = GetFinalPathNameByHandleA(h, raw, cap, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (wrote == 0 || wrote >= cap) return false;

    const char *view = raw;
    char *unc_fixed = NULL;
    if (strncmp(view, "\\\\?\\UNC\\", 8) == 0) {
        size_t rest = strlen(view + 8);
        unc_fixed = (char*)arena_alloc(eval_temp_arena(ctx), rest + 3);
        EVAL_OOM_RETURN_IF_NULL(ctx, unc_fixed, false);
        unc_fixed[0] = '/';
        unc_fixed[1] = '/';
        memcpy(unc_fixed + 2, view + 8, rest + 1);
        view = unc_fixed;
    } else if (strncmp(view, "\\\\?\\", 4) == 0) {
        view += 4;
    }

    size_t len = strlen(view);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, view, len + 1);
    scope_normalize_slashes_in_place(norm);
    *out_canon_cstr = norm;
    return true;
#else
    char *real = realpath(path_c, NULL);
    if (!real) return false;
    size_t len = strlen(real);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, real, len + 1);
    free(real);
    *out_canon_cstr = norm;
    return true;
#endif
}

static bool scope_canonicalize_existing_or_parent_temp(EvalExecContext *ctx,
                                                       String_View path,
                                                       String_View *out) {
    if (!ctx || !out || path.count == 0) return false;

    char *probe = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, probe, false);
    scope_normalize_slashes_in_place(probe);

    for (;;) {
        char *canon = NULL;
        if (scope_canonicalize_existing_cstr_temp(ctx, probe, &canon)) {
            *out = nob_sv_from_cstr(canon);
            return true;
        }

        size_t len = strlen(probe);
        while (len > 0 && probe[len - 1] == '/') {
            if (len == 1) break;
            if (len == 3 && isalpha((unsigned char)probe[0]) && probe[1] == ':') break;
            probe[--len] = '\0';
        }

        char *last = strrchr(probe, '/');
        if (!last) return false;

        if (last == probe) {
            probe[1] = '\0';
            continue;
        }
        if (last == probe + 2 && isalpha((unsigned char)probe[0]) && probe[1] == ':') {
            probe[3] = '\0';
            continue;
        }
        *last = '\0';
    }

    return false;
}

bool eval_file_resolve_path(EvalExecContext *ctx,
                            const Node *node,
                            Cmake_Event_Origin origin,
                            String_View input_path,
                            String_View relative_base,
                            Eval_File_Path_Mode mode,
                            String_View *out_path) {
    if (!ctx || !node || !out_path) return false;

    if (mode == EVAL_FILE_PATH_MODE_PROJECT_SCOPED) {
        if (!is_path_safe(input_path)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_file"), node->as.cmd.name, origin, nob_sv_from_cstr("Security Violation: Path traversal (..) is not allowed"), input_path);
            return false;
        }
    }

    String_View path = input_path;
    if (!eval_sv_is_abs_path(path)) {
        path = eval_sv_path_join(eval_temp_arena(ctx), relative_base, path);
    }
    path = eval_file_cmk_path_normalize_temp(ctx, path);
    if (eval_should_stop(ctx) || path.count == 0) return false;

    bool ci = false;
#if defined(_WIN32)
    ci = true;
#endif

    if (mode == EVAL_FILE_PATH_MODE_PROJECT_SCOPED) {
        if (!scope_path_has_prefix(path, ctx->binary_dir, ci) &&
            !scope_path_has_prefix(path, ctx->source_dir, ci)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_file"), node->as.cmd.name, origin, nob_sv_from_cstr("Security Violation: Absolute path outside project scope"), path);
            return false;
        }
    }

    String_View resolved_probe = nob_sv_from_cstr("");
    if (mode == EVAL_FILE_PATH_MODE_PROJECT_SCOPED &&
        scope_canonicalize_existing_or_parent_temp(ctx, path, &resolved_probe)) {
        String_View source_scope = ctx->source_dir;
        String_View binary_scope = ctx->binary_dir;

        String_View source_scope_canon = nob_sv_from_cstr("");
        if (scope_canonicalize_existing_or_parent_temp(ctx, ctx->source_dir, &source_scope_canon)) {
            source_scope = source_scope_canon;
        }

        String_View binary_scope_canon = nob_sv_from_cstr("");
        if (scope_canonicalize_existing_or_parent_temp(ctx, ctx->binary_dir, &binary_scope_canon)) {
            binary_scope = binary_scope_canon;
        }

        if (!scope_path_has_prefix(resolved_probe, binary_scope, ci) &&
            !scope_path_has_prefix(resolved_probe, source_scope, ci)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_file"), node->as.cmd.name, origin, nob_sv_from_cstr("Security Violation: Absolute path outside project scope"), input_path);
            return false;
        }
    }

    *out_path = path;
    return true;
}

bool eval_file_resolve_project_scoped_path(EvalExecContext *ctx,
                                           const Node *node,
                                           Cmake_Event_Origin origin,
                                           String_View input_path,
                                           String_View relative_base,
                                           String_View *out_path) {
    return eval_file_resolve_path(
        ctx, node, origin, input_path, relative_base, EVAL_FILE_PATH_MODE_CMAKE, out_path);
}

String_View eval_file_cmk_path_normalize_temp(EvalExecContext *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr("");

    bool is_unc = input.count >= 2 && svu_is_path_sep(input.data[0]) && svu_is_path_sep(input.data[1]);
    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;

    if (is_unc) {
        pos = 2;
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

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 3);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (is_unc) {
        buf[off++] = '/';
        buf[off++] = '/';
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    } else if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
        if (absolute) buf[off++] = '/';
    } else if (absolute) {
        buf[off++] = '/';
    }

    bool prev_sep = (off > 0 && buf[off - 1] == '/');
    for (; pos < input.count; pos++) {
        char c = input.data[pos];
        if (svu_is_path_sep(c)) {
            if (!prev_sep) {
                buf[off++] = '/';
                prev_sep = true;
            }
            continue;
        }
        buf[off++] = c;
        prev_sep = false;
    }

    size_t min_len = 0;
    if (is_unc) min_len = 2;
    else if (has_drive && absolute) min_len = 3;
    else if (has_drive) min_len = 2;
    else if (absolute) min_len = 1;

    while (off > min_len && buf[off - 1] == '/') off--;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_file_canonicalize_existing_path_temp(EvalExecContext *ctx, String_View path, String_View *out_path) {
    if (!ctx || !out_path || path.count == 0) return false;
    *out_path = nob_sv_from_cstr("");

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    scope_normalize_slashes_in_place(path_c);

    char *canon = NULL;
    if (!scope_canonicalize_existing_cstr_temp(ctx, path_c, &canon)) return false;
    *out_path = nob_sv_from_cstr(canon);
    return true;
}

static size_t mkdir_root_prefix_len(const char *path) {
    if (!path) return 0;
    size_t len = strlen(path);
    if (len == 0) return 0;

    if (len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/') {
        return 3;
    }
    if (len >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        return 2;
    }
    if (len >= 2 && path[0] == '/' && path[1] == '/') {
        size_t i = 2;
        while (i < len && path[i] == '/') i++;
        while (i < len && path[i] != '/') i++;
        while (i < len && path[i] == '/') i++;
        while (i < len && path[i] != '/') i++;
        return i;
    }
    if (path[0] == '/') return 1;
    return 0;
}

bool eval_file_mkdir_p(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;

    String_View normalized = eval_file_cmk_path_normalize_temp(ctx, path);
    if (eval_should_stop(ctx) || normalized.count == 0) return false;

    char *path_c = eval_sv_to_cstr_temp(ctx, normalized);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    size_t len0 = strlen(path_c);
    char *tmp = (char*)arena_alloc(eval_temp_arena(ctx), len0 + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, tmp, false);
    memcpy(tmp, path_c, len0 + 1);

    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    size_t len = strlen(tmp);
    size_t prefix_len = mkdir_root_prefix_len(tmp);
    while (len > prefix_len && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }

    size_t start = prefix_len;
    while (start < len && tmp[start] == '/') start++;
    if (start >= len) {
        return true;
    }

    for (size_t i = start; i < len; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (!nob_mkdir_if_not_exists(tmp)) {
            return false;
        }
        tmp[i] = '/';
    }

    return nob_mkdir_if_not_exists(tmp);
}
