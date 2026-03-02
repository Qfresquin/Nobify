#include "eval_file_internal.h"
#include "eval_expr.h"
#include "sv_utils.h"
#include "arena_dyn.h"
#include "tinydir.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

static bool file_parse_permission_token(mode_t *mode, String_View token) {
    if (!mode) return false;
    if (eval_sv_eq_ci_lit(token, "OWNER_READ")) {
#ifdef S_IRUSR
        *mode |= S_IRUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_WRITE")) {
#ifdef S_IWUSR
        *mode |= S_IWUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "OWNER_EXECUTE")) {
#ifdef S_IXUSR
        *mode |= S_IXUSR;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_READ")) {
#ifdef S_IRGRP
        *mode |= S_IRGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_WRITE")) {
#ifdef S_IWGRP
        *mode |= S_IWGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "GROUP_EXECUTE")) {
#ifdef S_IXGRP
        *mode |= S_IXGRP;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_READ")) {
#ifdef S_IROTH
        *mode |= S_IROTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_WRITE")) {
#ifdef S_IWOTH
        *mode |= S_IWOTH;
#endif
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "WORLD_EXECUTE")) {
#ifdef S_IXOTH
        *mode |= S_IXOTH;
#endif
        return true;
    }
    return false;
}

static bool file_leaf_exists(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st = {0};
    return lstat(path, &st) == 0;
#endif
}

static String_View file_path_join_temp_checked(Evaluator_Context *ctx, String_View a, String_View b) {
    if (!ctx) return nob_sv_from_cstr("");
    if (a.count == 0) return sv_copy_to_temp_arena(ctx, b);
    if (b.count == 0) return sv_copy_to_temp_arena(ctx, a);

    bool need_slash = !svu_is_path_sep(a.data[a.count - 1]);
    size_t total = a.count + (need_slash ? 1u : 0u) + b.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, a.data, a.count);
    off += a.count;
    if (need_slash) buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool file_real_path_is_root_cstr(const char *path) {
    if (!path || path[0] == '\0') return true;
    size_t len = strlen(path);
    if (len == 1 && svu_is_path_sep(path[0])) return true;
    if (len == 2 && svu_is_path_sep(path[0]) && svu_is_path_sep(path[1])) return true;
    if (len == 3 &&
        isalpha((unsigned char)path[0]) &&
        path[1] == ':' &&
        svu_is_path_sep(path[2])) {
        return true;
    }
    return false;
}

static bool file_real_path_pop_last_component(Evaluator_Context *ctx, char *path_c, String_View *io_suffix) {
    if (!ctx || !path_c || !io_suffix) return false;

    size_t len = strlen(path_c);
    while (len > 0 && svu_is_path_sep(path_c[len - 1])) {
        if (len == 1) break;
        if (len == 3 && isalpha((unsigned char)path_c[0]) && path_c[1] == ':') break;
        path_c[--len] = '\0';
    }

    if (len == 0 || file_real_path_is_root_cstr(path_c)) return false;

    size_t last_sep = SIZE_MAX;
    for (size_t i = len; i-- > 0;) {
        if (svu_is_path_sep(path_c[i])) {
            last_sep = i;
            break;
        }
    }

    size_t removed_start = 0;
    size_t new_len = 0;
    if (last_sep == SIZE_MAX) {
        removed_start = 0;
        new_len = 0;
    } else if (last_sep == 0) {
        removed_start = 1;
        new_len = 1;
    } else if (last_sep == 2 && isalpha((unsigned char)path_c[0]) && path_c[1] == ':') {
        removed_start = 3;
        new_len = 3;
    } else {
        removed_start = last_sep + 1;
        new_len = last_sep;
    }

    String_View removed = nob_sv_from_parts(path_c + removed_start, len - removed_start);
    if (removed.count == 0) return false;

    String_View suffix = removed;
    if (io_suffix->count > 0) {
        suffix = file_path_join_temp_checked(ctx, removed, *io_suffix);
        if (eval_should_stop(ctx)) return false;
    }
    *io_suffix = suffix;
    path_c[new_len] = '\0';
    return true;
}

static bool file_real_path_resolve_temp(Evaluator_Context *ctx,
                                        String_View path,
                                        bool cmp0152_new,
                                        String_View *out_path) {
    if (!ctx || !out_path) return false;
    *out_path = nob_sv_from_cstr("");

    String_View seed = cmp0152_new ? eval_file_cmk_path_normalize_temp(ctx, path)
                                   : eval_sv_path_normalize_temp(ctx, path);
    if (eval_should_stop(ctx)) return false;
    if (seed.count == 0) {
        *out_path = seed;
        return true;
    }

    String_View canonical = nob_sv_from_cstr("");
    if (eval_file_canonicalize_existing_path_temp(ctx, seed, &canonical)) {
        *out_path = canonical;
        return true;
    }
    if (eval_should_stop(ctx)) return false;

    char *probe = eval_sv_to_cstr_temp(ctx, seed);
    EVAL_OOM_RETURN_IF_NULL(ctx, probe, false);

    String_View suffix = nob_sv_from_cstr("");
    while (probe[0] != '\0' && !file_real_path_is_root_cstr(probe)) {
        if (!file_real_path_pop_last_component(ctx, probe, &suffix)) return false;

        String_View prefix = nob_sv_from_cstr("");
        if (!eval_file_canonicalize_existing_path_temp(ctx, nob_sv_from_cstr(probe), &prefix)) {
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        String_View combined = prefix;
        if (suffix.count > 0) {
            combined = file_path_join_temp_checked(ctx, prefix, suffix);
            if (eval_should_stop(ctx)) return false;
        }
        *out_path = eval_file_cmk_path_normalize_temp(ctx, combined);
        return !eval_should_stop(ctx);
    }

    if (probe[0] != '\0') {
        String_View prefix = nob_sv_from_cstr("");
        if (eval_file_canonicalize_existing_path_temp(ctx, nob_sv_from_cstr(probe), &prefix)) {
            String_View combined = prefix;
            if (suffix.count > 0) {
                combined = file_path_join_temp_checked(ctx, prefix, suffix);
                if (eval_should_stop(ctx)) return false;
            }
            *out_path = eval_file_cmk_path_normalize_temp(ctx, combined);
            return !eval_should_stop(ctx);
        }
        if (eval_should_stop(ctx)) return false;
    }

    *out_path = eval_file_cmk_path_normalize_temp(ctx, seed);
    return !eval_should_stop(ctx);
}

static bool file_remove_leaf(const char *path, bool is_dir) {
    if (!path) return false;
#if defined(_WIN32)
    if (is_dir) return RemoveDirectoryA(path) != 0;
    return DeleteFileA(path) != 0;
#else
    if (is_dir) return rmdir(path) == 0;
    return unlink(path) == 0;
#endif
}

static bool file_remove_tree_recursive(const char *path) {
    if (!path || path[0] == '\0') return false;
    if (!file_leaf_exists(path)) return true;

    struct stat st = {0};
    if (stat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) return file_remove_leaf(path, false);

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, path) != 0) return false;

    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            ok = false;
            break;
        }
        if (strcmp(file.name, ".") == 0 || strcmp(file.name, "..") == 0) continue;

        if (file.is_dir) {
            if (!file_remove_tree_recursive(file.path)) {
                ok = false;
                break;
            }
        } else {
            if (!file_remove_leaf(file.path, false)) {
                ok = false;
                break;
            }
        }
    }
    tinydir_close(&dir);
    if (!ok) return false;
    return file_remove_leaf(path, true);
}

static bool file_apply_mode_one(const char *path, mode_t mode) {
    if (!path) return false;
#if defined(_WIN32)
    (void)mode;
    return _chmod(path, _S_IREAD | _S_IWRITE) == 0;
#else
    return chmod(path, mode) == 0;
#endif
}

static bool file_chmod_recursive_c(const char *path, mode_t mode) {
    if (!path || path[0] == '\0') return false;
    if (!file_leaf_exists(path)) return false;

    struct stat st = {0};
    if (lstat(path, &st) != 0) return false;

    bool is_dir = S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode);
    if (!file_apply_mode_one(path, mode)) return false;
    if (!is_dir) return true;

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, path) != 0) return false;
    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            ok = false;
            break;
        }
        if (strcmp(file.name, ".") == 0 || strcmp(file.name, "..") == 0) continue;
        if (!file_chmod_recursive_c(file.path, mode)) {
            ok = false;
            break;
        }
    }
    tinydir_close(&dir);
    return ok;
}

static bool file_prepare_parent_dir(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    char *dir_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, dir_c, false);

    char *last = strrchr(dir_c, '/');
    if (!last) last = strrchr(dir_c, '\\');
    if (!last) return true;
    *last = '\0';
    if (dir_c[0] == '\0') return true;
    return eval_file_mkdir_p(ctx, nob_sv_from_cstr(dir_c));
}

static bool file_emit_result_code(Evaluator_Context *ctx, String_View out_var, int code, const char *message) {
    if (!ctx || out_var.count == 0) return false;
    String_View v = nob_sv_from_cstr(nob_temp_sprintf("%d;%s", code, message ? message : ""));
    return eval_var_set(ctx, out_var, v);
}

static String_View file_relativize_temp(Evaluator_Context *ctx, String_View full, String_View base) {
    if (!ctx) return nob_sv_from_cstr("");
    full = eval_file_cmk_path_normalize_temp(ctx, full);
    base = eval_file_cmk_path_normalize_temp(ctx, base);

    char *f = eval_sv_to_cstr_temp(ctx, full);
    char *b = eval_sv_to_cstr_temp(ctx, base);
    EVAL_OOM_RETURN_IF_NULL(ctx, f, nob_sv_from_cstr(""));
    EVAL_OOM_RETURN_IF_NULL(ctx, b, nob_sv_from_cstr(""));

    bool ci = false;
#if defined(_WIN32)
    ci = true;
#endif

    size_t i = 0;
    size_t shared = 0;
    while (f[i] && b[i]) {
        char fc = f[i];
        char bc = b[i];
        if (ci) {
            fc = (char)tolower((unsigned char)fc);
            bc = (char)tolower((unsigned char)bc);
        }
        if (fc != bc) break;
        if (fc == '/') shared = i + 1;
        i++;
    }
    if (f[i] == '\0' && b[i] == '\0') return nob_sv_from_cstr(".");

    size_t ups = 0;
    for (size_t j = shared; b[j] != '\0'; j++) {
        if (b[j] == '/') ups++;
    }
    if (shared < strlen(b)) ups++;

    size_t remain = strlen(f + shared);
    size_t cap = (ups * 3) + remain + 1;
    char *out = (char*)arena_alloc(eval_temp_arena(ctx), cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, out, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t k = 0; k < ups; k++) {
        out[off++] = '.';
        out[off++] = '.';
        out[off++] = '/';
    }
    if (remain > 0) {
        memcpy(out + off, f + shared, remain);
        off += remain;
    } else if (off > 0) {
        off--;
    } else {
        out[off++] = '.';
    }
    out[off] = '\0';
    return nob_sv_from_cstr(out);
}

static String_View file_convert_path_list_temp(Evaluator_Context *ctx, String_View in, bool to_cmake) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

#if defined(_WIN32)
    char native_list_sep = ';';
    char native_sep = '\\';
#else
    char native_list_sep = ':';
    char native_sep = '/';
#endif

    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (to_cmake) {
            if (c == native_list_sep && native_list_sep != ';') c = ';';
            if (c == '\\') c = '/';
        } else {
            if (c == ';') c = native_list_sep;
            if (c == '/') c = native_sep;
        }
        buf[i] = c;
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool handle_file_append(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(APPEND) requires <path> and <content>"),
                       nob_sv_from_cstr("Usage: file(APPEND <path> <content>...)"));
        return true;
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_bin_dir(ctx), &path)) return true;
    if (!file_prepare_parent_dir(ctx, path)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(APPEND) failed to create parent directory"), path);
        return true;
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);
    FILE *f = fopen(path_c, "ab");
    if (!f) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(APPEND) failed to open file"), path);
        return true;
    }
    for (size_t i = 2; i < args.count; i++) {
        fwrite(args.items[i].data, 1, args.items[i].count, f);
    }
    fclose(f);
    return true;
}

static bool handle_file_size(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(SIZE) requires <path> and <out-var>"),
                       nob_sv_from_cstr("Usage: file(SIZE <path> <out-var>)"));
        return true;
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &path)) return true;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);

    struct stat st = {0};
    if (stat(path_c, &st) != 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(SIZE) failed to stat file"), path);
        return true;
    }
    (void)eval_var_set(ctx, args.items[2], nob_sv_from_cstr(nob_temp_sprintf("%lld", (long long)st.st_size)));
    return true;
}

static bool handle_file_rename(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(RENAME) requires old and new path"),
                       nob_sv_from_cstr("Usage: file(RENAME <old> <new> [NO_REPLACE] [RESULT <var>])"));
        return true;
    }

    bool no_replace = false;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "NO_REPLACE")) {
            no_replace = true;
        } else if (eval_sv_eq_ci_lit(args.items[i], "RESULT") && i + 1 < args.count) {
            result_var = args.items[++i];
        }
    }

    String_View old_path = nob_sv_from_cstr("");
    String_View new_path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_bin_dir(ctx), &old_path)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[2], eval_file_current_bin_dir(ctx), &new_path)) return true;

    char *old_c = eval_sv_to_cstr_temp(ctx, old_path);
    char *new_c = eval_sv_to_cstr_temp(ctx, new_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, old_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, new_c, true);

    if (!file_prepare_parent_dir(ctx, new_path)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(RENAME) failed to create destination directory"), new_path);
        return true;
    }

    if (no_replace && file_leaf_exists(new_c)) {
        if (result_var.count > 0) (void)file_emit_result_code(ctx, result_var, 1, "NO_REPLACE");
        return true;
    }

    if (!nob_rename(old_c, new_c)) {
        if (result_var.count > 0) {
            (void)file_emit_result_code(ctx, result_var, 1, "rename failed");
            return true;
        }
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(RENAME) failed"), old_path);
        return true;
    }

    if (result_var.count > 0) (void)file_emit_result_code(ctx, result_var, 0, "OK");
    return true;
}

static bool handle_file_remove(Evaluator_Context *ctx, const Node *node, SV_List args, bool recurse) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 2) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       recurse ? nob_sv_from_cstr("file(REMOVE_RECURSE) requires at least one path")
                               : nob_sv_from_cstr("file(REMOVE) requires at least one path"),
                       recurse ? nob_sv_from_cstr("Usage: file(REMOVE_RECURSE <path>...)")
                               : nob_sv_from_cstr("Usage: file(REMOVE <path>...)"));
        return true;
    }

    for (size_t i = 1; i < args.count; i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[i], eval_file_current_bin_dir(ctx), &path)) return true;
        char *path_c = eval_sv_to_cstr_temp(ctx, path);
        EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);

        bool ok = recurse ? file_remove_tree_recursive(path_c) : file_remove_leaf(path_c, false);
        if (!ok && !file_leaf_exists(path_c)) ok = true;
        if (!ok) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           recurse ? nob_sv_from_cstr("file(REMOVE_RECURSE) failed to remove path")
                                   : nob_sv_from_cstr("file(REMOVE) failed to remove path"),
                           path);
            return true;
        }
    }
    return true;
}

static bool handle_file_read_symlink(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(READ_SYMLINK) requires path and out-var"),
                       nob_sv_from_cstr("Usage: file(READ_SYMLINK <path> <out-var>)"));
        return true;
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &path)) return true;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);

#if defined(_WIN32)
    eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                   nob_sv_from_cstr("file(READ_SYMLINK) is not supported on Windows backend"),
                   path);
    return true;
#else
    char tmp[PATH_MAX];
    ssize_t n = readlink(path_c, tmp, sizeof(tmp) - 1);
    if (n < 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(READ_SYMLINK) failed"), path);
        return true;
    }
    tmp[n] = '\0';
    (void)eval_var_set(ctx, args.items[2], nob_sv_from_cstr(tmp));
    return true;
#endif
}

static bool handle_file_create_link(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CREATE_LINK) requires source and link path"),
                       nob_sv_from_cstr("Usage: file(CREATE_LINK <orig> <link> [SYMBOLIC] [COPY_ON_ERROR] [RESULT <var>])"));
        return true;
    }

    bool symbolic = false;
    bool copy_on_error = false;
    String_View result_var = nob_sv_from_cstr("");
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "SYMBOLIC")) symbolic = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "COPY_ON_ERROR")) copy_on_error = true;
        else if (eval_sv_eq_ci_lit(args.items[i], "RESULT") && i + 1 < args.count) result_var = args.items[++i];
    }

    String_View src = nob_sv_from_cstr("");
    String_View dst = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &src)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[2], eval_file_current_bin_dir(ctx), &dst)) return true;
    if (!file_prepare_parent_dir(ctx, dst)) return true;

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, true);

    bool ok = false;
#if defined(_WIN32)
    if (symbolic) {
        DWORD flags = 0;
        if (CreateSymbolicLinkA(dst_c, src_c, flags) != 0) ok = true;
    } else {
        if (CreateHardLinkA(dst_c, src_c, NULL) != 0) ok = true;
    }
#else
    if (symbolic) ok = symlink(src_c, dst_c) == 0;
    else ok = link(src_c, dst_c) == 0;
#endif

    if (!ok && copy_on_error) ok = nob_copy_file(src_c, dst_c);

    if (!ok) {
        if (result_var.count > 0) {
            (void)file_emit_result_code(ctx, result_var, 1, "CREATE_LINK failed");
            return true;
        }
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CREATE_LINK) failed"), dst);
        return true;
    }

    if (result_var.count > 0) (void)file_emit_result_code(ctx, result_var, 0, "OK");
    return true;
}

static bool parse_mode_from_args(Evaluator_Context *ctx, const Node *node, Cmake_Event_Origin o, SV_List values, mode_t *out_mode) {
    if (!out_mode) return false;
    mode_t mode = 0;
    bool has_any = false;
    for (size_t i = 0; i < values.count; i++) {
        if (file_parse_permission_token(&mode, values.items[i])) {
            has_any = true;
            continue;
        }
        eval_emit_diag(ctx, EV_DIAG_WARNING, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CHMOD) unknown permission token"), values.items[i]);
    }
    if (!has_any) return false;
    *out_mode = mode;
    return true;
}

static bool handle_file_chmod(Evaluator_Context *ctx, const Node *node, SV_List args, bool recurse) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       recurse ? nob_sv_from_cstr("file(CHMOD_RECURSE) requires paths and PERMISSIONS")
                               : nob_sv_from_cstr("file(CHMOD) requires paths and PERMISSIONS"),
                       recurse ? nob_sv_from_cstr("Usage: file(CHMOD_RECURSE <path>... PERMISSIONS <tokens...>)")
                               : nob_sv_from_cstr("Usage: file(CHMOD <path>... PERMISSIONS <tokens...>)"));
        return true;
    }

    size_t perm_idx = SIZE_MAX;
    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "PERMISSIONS")) {
            perm_idx = i;
            break;
        }
    }
    if (perm_idx == SIZE_MAX || perm_idx + 1 >= args.count || perm_idx == 1) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CHMOD) missing PERMISSIONS or paths"),
                       nob_sv_from_cstr("Usage: file(CHMOD <path>... PERMISSIONS <tokens...>)"));
        return true;
    }

    SV_List perm_values = {
        .items = &args.items[perm_idx + 1],
        .count = args.count - (perm_idx + 1),
        .capacity = 0,
    };
    mode_t mode = 0;
    if (!parse_mode_from_args(ctx, node, o, perm_values, &mode)) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(CHMOD) has no valid permission token"), nob_sv_from_cstr(""));
        return true;
    }

    for (size_t i = 1; i < perm_idx; i++) {
        String_View path = nob_sv_from_cstr("");
        if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[i], eval_file_current_bin_dir(ctx), &path)) return true;
        char *path_c = eval_sv_to_cstr_temp(ctx, path);
        EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);
        bool ok = recurse ? file_chmod_recursive_c(path_c, mode) : file_apply_mode_one(path_c, mode);
        if (!ok) {
            eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                           recurse ? nob_sv_from_cstr("file(CHMOD_RECURSE) failed")
                                   : nob_sv_from_cstr("file(CHMOD) failed"),
                           path);
            return true;
        }
    }
    return true;
}

static bool handle_file_real_path(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(REAL_PATH) requires input and out-var"),
                       nob_sv_from_cstr("Usage: file(REAL_PATH <path> <out-var> [BASE_DIRECTORY <dir>] [EXPAND_TILDE])"));
        return true;
    }

    String_View base_dir = eval_file_current_src_dir(ctx);
    bool expand_tilde = false;
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "BASE_DIRECTORY") && i + 1 < args.count) {
            base_dir = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "EXPAND_TILDE")) {
            expand_tilde = true;
        }
    }

    String_View input = args.items[1];
    if (expand_tilde && input.count > 0 && input.data[0] == '~') {
        const char *home = eval_getenv_temp(ctx, "HOME");
        if (home && home[0] != '\0') {
            String_View suffix = nob_sv_from_parts(input.data + 1, input.count - 1);
            String_View hv = nob_sv_from_cstr(home);
            input = eval_sv_path_join(eval_temp_arena(ctx), hv, suffix);
        }
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, input, base_dir, &path)) return true;
    bool cmp0152_new = eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr("CMP0152")), "NEW");
    String_View resolved = nob_sv_from_cstr("");
    if (!file_real_path_resolve_temp(ctx, path, cmp0152_new, &resolved)) return true;
    (void)eval_var_set(ctx, args.items[2], resolved);
    return true;
}

static bool handle_file_relative_path(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 4) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(RELATIVE_PATH) requires out-var, directory and file"),
                       nob_sv_from_cstr("Usage: file(RELATIVE_PATH <out-var> <directory> <file>)"));
        return true;
    }

    String_View dir = nob_sv_from_cstr("");
    String_View file = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[2], eval_file_current_src_dir(ctx), &dir)) return true;
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[3], eval_file_current_src_dir(ctx), &file)) return true;
    (void)eval_var_set(ctx, args.items[1], file_relativize_temp(ctx, file, dir));
    return true;
}

static bool handle_file_to_path(Evaluator_Context *ctx, const Node *node, SV_List args, bool to_cmake) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       to_cmake ? nob_sv_from_cstr("file(TO_CMAKE_PATH) requires input and out-var")
                                : nob_sv_from_cstr("file(TO_NATIVE_PATH) requires input and out-var"),
                       to_cmake ? nob_sv_from_cstr("Usage: file(TO_CMAKE_PATH <path> <out-var>)")
                                : nob_sv_from_cstr("Usage: file(TO_NATIVE_PATH <path> <out-var>)"));
        return true;
    }
    (void)eval_var_set(ctx, args.items[2], file_convert_path_list_temp(ctx, args.items[1], to_cmake));
    return true;
}

static bool handle_file_timestamp(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 3) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(TIMESTAMP) requires path and out-var"),
                       nob_sv_from_cstr("Usage: file(TIMESTAMP <path> <out-var> [format] [UTC])"));
        return true;
    }

    bool utc = false;
    String_View fmt = nob_sv_from_cstr("%Y-%m-%dT%H:%M:%S");
    for (size_t i = 3; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "UTC")) utc = true;
        else fmt = args.items[i];
    }

    String_View path = nob_sv_from_cstr("");
    if (!eval_file_resolve_project_scoped_path(ctx, node, o, args.items[1], eval_file_current_src_dir(ctx), &path)) return true;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    char *fmt_c = eval_sv_to_cstr_temp(ctx, fmt);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, true);
    EVAL_OOM_RETURN_IF_NULL(ctx, fmt_c, true);

    struct stat st = {0};
    if (stat(path_c, &st) != 0) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(TIMESTAMP) failed to stat file"), path);
        return true;
    }

    struct tm tmv = {0};
#if defined(_WIN32)
    errno_t rc = utc ? gmtime_s(&tmv, &st.st_mtime) : localtime_s(&tmv, &st.st_mtime);
    if (rc != 0) return true;
#else
    if (!(utc ? gmtime_r(&st.st_mtime, &tmv) : localtime_r(&st.st_mtime, &tmv))) return true;
#endif
    char *out = (char*)arena_alloc(eval_temp_arena(ctx), 256);
    EVAL_OOM_RETURN_IF_NULL(ctx, out, true);
    size_t n = strftime(out, 256, fmt_c, &tmv);
    out[n] = '\0';
    (void)eval_var_set(ctx, args.items[2], nob_sv_from_cstr(out));
    return true;
}

static bool handle_file_install(Evaluator_Context *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (args.count < 4) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(INSTALL) requires sources and DESTINATION"),
                       nob_sv_from_cstr("Usage: file(INSTALL <src>... DESTINATION <dir>)"));
        return true;
    }

    SV_List copy_args = {0};
    if (!arena_da_reserve(eval_temp_arena(ctx), (void**)&copy_args.items, &copy_args.capacity, sizeof(String_View), args.count + 4)) {
        ctx_oom(ctx);
        return true;
    }
    copy_args.items[copy_args.count++] = nob_sv_from_cstr("COPY");

    size_t dest_idx = SIZE_MAX;
    for (size_t i = 1; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "DESTINATION")) {
            dest_idx = i;
            break;
        }
    }
    if (dest_idx == SIZE_MAX || dest_idx + 1 >= args.count) {
        eval_emit_diag(ctx, EV_DIAG_ERROR, nob_sv_from_cstr("eval_file"), node->as.cmd.name, o,
                       nob_sv_from_cstr("file(INSTALL) missing DESTINATION"), nob_sv_from_cstr(""));
        return true;
    }

    for (size_t i = 1; i < dest_idx; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "TYPE") && i + 1 < dest_idx) {
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "FILES") ||
            eval_sv_eq_ci_lit(args.items[i], "PROGRAMS") ||
            eval_sv_eq_ci_lit(args.items[i], "DIRECTORY")) {
            continue;
        }
        copy_args.items[copy_args.count++] = args.items[i];
    }

    copy_args.items[copy_args.count++] = nob_sv_from_cstr("DESTINATION");
    copy_args.items[copy_args.count++] = args.items[dest_idx + 1];

    for (size_t i = dest_idx + 2; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "TYPE") && i + 1 < args.count) {
            i++;
            continue;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "OPTIONAL") ||
            eval_sv_eq_ci_lit(args.items[i], "MESSAGE_NEVER")) {
            continue;
        }
        copy_args.items[copy_args.count++] = args.items[i];
    }

    eval_file_handle_copy(ctx, node, copy_args);
    return true;
}

bool eval_file_handle_fsops(Evaluator_Context *ctx, const Node *node, SV_List args) {
    if (!ctx || !node || args.count == 0) return false;
    String_View subcmd = args.items[0];

    if (eval_sv_eq_ci_lit(subcmd, "APPEND")) return handle_file_append(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "INSTALL")) return handle_file_install(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "SIZE")) return handle_file_size(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "RENAME")) return handle_file_rename(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "REMOVE")) return handle_file_remove(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(subcmd, "REMOVE_RECURSE")) return handle_file_remove(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(subcmd, "READ_SYMLINK")) return handle_file_read_symlink(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "CREATE_LINK")) return handle_file_create_link(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "CHMOD")) return handle_file_chmod(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(subcmd, "CHMOD_RECURSE")) return handle_file_chmod(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(subcmd, "REAL_PATH")) return handle_file_real_path(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "RELATIVE_PATH")) return handle_file_relative_path(ctx, node, args);
    if (eval_sv_eq_ci_lit(subcmd, "TO_CMAKE_PATH")) return handle_file_to_path(ctx, node, args, true);
    if (eval_sv_eq_ci_lit(subcmd, "TO_NATIVE_PATH")) return handle_file_to_path(ctx, node, args, false);
    if (eval_sv_eq_ci_lit(subcmd, "TIMESTAMP")) return handle_file_timestamp(ctx, node, args);

    return false;
}
