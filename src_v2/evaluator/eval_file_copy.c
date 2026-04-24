#include "eval_file_internal.h"
#include "eval_opt_parser.h"
#include "sv_utils.h"
#include "arena_dyn.h"

#include <pcre2posix.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#endif

typedef struct {
    bool is_regex;
    String_View expr;
    bool exclude;
    bool regex_ready;
    regex_t regex;
} Copy_Filter;

typedef struct {
    bool has_permissions;
    bool has_file_permissions;
    bool has_directory_permissions;
    mode_t permissions_mode;
    mode_t file_permissions_mode;
    mode_t directory_permissions_mode;
    bool saw_use_source_permissions;
    bool saw_no_source_permissions;
} Copy_Permissions;

enum {
    COPY_KEY_DESTINATION = 0,
    COPY_KEY_FILES_MATCHING,
    COPY_KEY_PATTERN,
    COPY_KEY_REGEX,
    COPY_KEY_EXCLUDE,
    COPY_KEY_FOLLOW_SYMLINK_CHAIN,
    COPY_KEY_PERMISSIONS,
    COPY_KEY_FILE_PERMISSIONS,
    COPY_KEY_DIRECTORY_PERMISSIONS,
    COPY_KEY_USE_SOURCE_PERMISSIONS,
    COPY_KEY_NO_SOURCE_PERMISSIONS,
    COPY_KEY_UNKNOWN,
};

static bool copy_permission_add_token(mode_t *mode, String_View token) {
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

static bool copy_permissions_pick_mode(const Copy_Permissions *perms, bool is_dir, mode_t *out_mode) {
    if (!perms || !out_mode) return false;
    if (is_dir && perms->has_directory_permissions) {
        *out_mode = perms->directory_permissions_mode;
        return true;
    }
    if (!is_dir && perms->has_file_permissions) {
        *out_mode = perms->file_permissions_mode;
        return true;
    }
    if (perms->has_permissions) {
        *out_mode = perms->permissions_mode;
        return true;
    }
    return false;
}

static String_View copy_mode_arg_temp(EvalExecContext *ctx, mode_t mode, bool has_mode) {
    if (!has_mode) return nob_sv_from_cstr("");
    return eval_replay_mode_octal_temp(ctx, (unsigned int)mode);
}

static bool copy_emit_unsupported_replay_marker(EvalExecContext *ctx,
                                                Cmake_Event_Origin origin,
                                                String_View reason) {
    String_View action_key = nob_sv_from_cstr("");
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_FILESYSTEM,
                                  EVENT_REPLAY_OPCODE_NONE,
                                  eval_replay_phase_for_filesystem_effect(ctx, EVENT_REPLAY_PHASE_CONFIGURE),
                                  eval_current_binary_dir(ctx),
                                  &action_key)) {
        return false;
    }
    (void)action_key;
    (void)reason;
    return true;
}

static bool copy_emit_replay(EvalExecContext *ctx,
                             Cmake_Event_Origin origin,
                             String_View src,
                             String_View dst,
                             bool src_is_dir,
                             const Copy_Permissions *perms,
                             bool use_source_permissions,
                             const char *src_c) {
    String_View action_key = nob_sv_from_cstr("");
    Event_Replay_Opcode opcode = src_is_dir ? EVENT_REPLAY_OPCODE_FS_COPY_TREE
                                            : EVENT_REPLAY_OPCODE_FS_COPY_FILE;
    String_View file_mode = nob_sv_from_cstr("");
    String_View dir_mode = nob_sv_from_cstr("");
    mode_t mode = 0;
    if (!ctx) return false;

    if (perms) {
        if (src_is_dir) {
            if (perms->has_file_permissions) {
                file_mode = copy_mode_arg_temp(ctx, perms->file_permissions_mode, true);
            } else if (perms->has_permissions) {
                file_mode = copy_mode_arg_temp(ctx, perms->permissions_mode, true);
            }
            if (perms->has_directory_permissions) {
                dir_mode = copy_mode_arg_temp(ctx, perms->directory_permissions_mode, true);
            } else if (perms->has_permissions) {
                dir_mode = copy_mode_arg_temp(ctx, perms->permissions_mode, true);
            }
        } else if (copy_permissions_pick_mode(perms, false, &mode)) {
            file_mode = copy_mode_arg_temp(ctx, mode, true);
        } else if (use_source_permissions && src_c) {
#if !defined(_WIN32)
            struct stat src_st = {0};
            if (stat(src_c, &src_st) == 0) {
                file_mode = copy_mode_arg_temp(ctx, src_st.st_mode & 0777, true);
            }
#else
            (void)src_c;
#endif
        }
    }

    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_FILESYSTEM,
                                  opcode,
                                  eval_replay_phase_for_filesystem_effect(ctx, EVENT_REPLAY_PHASE_CONFIGURE),
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_input(ctx, origin, action_key, src) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, dst)) {
        return false;
    }

    if (src_is_dir) {
        return eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, file_mode) &&
               eval_emit_replay_action_add_argv(ctx, origin, action_key, 1, dir_mode);
    }
    return eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, file_mode);
}

static bool copy_apply_permissions(const char *path, mode_t mode) {
    if (!path) return false;
#if defined(_WIN32)
    (void)mode;
    return true;
#else
    return chmod(path, mode) == 0;
#endif
}

static String_View copy_basename_sv(String_View path) {
    size_t base = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') base = i + 1;
    }
    return nob_sv_from_parts(path.data + base, path.count - base);
}

static bool copy_copy_entry_raw(const char *src_c, const char *dst_c, bool *out_src_is_dir) {
    bool src_is_dir = false;
    bool ok = false;
    if (!src_c || !dst_c) return false;

    if (nob_file_exists(src_c)) {
        Nob_File_Type kind = nob_get_file_type(src_c);
        if ((int)kind < 0) return false;
        src_is_dir = kind == NOB_FILE_DIRECTORY;
        ok = src_is_dir ? nob_copy_directory_recursively(src_c, dst_c)
                        : nob_copy_file(src_c, dst_c);
    } else {
        ok = nob_copy_file(src_c, dst_c);
    }
    if (out_src_is_dir) *out_src_is_dir = src_is_dir;
    return ok;
}

static bool copy_path_is_symlink(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#elif defined(S_ISLNK)
    struct stat st = {0};
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
#else
    return false;
#endif
}

static bool copy_read_symlink_target_temp(EvalExecContext *ctx, const char *path, String_View *out_target) {
    if (!ctx || !path || !out_target) return false;
#if defined(_WIN32)
    HANDLE h = CreateFileA(path,
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
    for (size_t i = 0; i < len; i++) {
        if (norm[i] == '\\') norm[i] = '/';
    }
    *out_target = nob_sv_from_cstr(norm);
    return true;
#else
    char buf[4096];
    ssize_t n = readlink(path, buf, sizeof(buf) - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    *out_target = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    if (eval_should_stop(ctx)) return false;
    return true;
#endif
}

#if !defined(_WIN32)
static bool copy_remove_existing_leaf(const char *path) {
    if (!path || path[0] == '\0') return false;
    struct stat st = {0};
    if (lstat(path, &st) != 0) return errno == ENOENT;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) return false;
    return remove(path) == 0;
}

static bool copy_create_symlink_like(const char *target, const char *link_path) {
    if (!target || !link_path) return false;
    if (!copy_remove_existing_leaf(link_path)) return false;
    return symlink(target, link_path) == 0;
}
#endif

static bool copy_follow_symlink_chain(EvalExecContext *ctx,
                                      const Node *node,
                                      Cmake_Event_Origin o,
                                      String_View src,
                                      String_View dest_dir,
                                      const Copy_Permissions *perms,
                                      bool *io_applied_any_permissions) {
    if (!ctx || !node) return false;
    String_View current = src;
#if defined(_WIN32)
    String_View *link_names = NULL;
#endif

    for (size_t depth = 0; depth < 64; depth++) {
        char *current_c = eval_sv_to_cstr_temp(ctx, current);
        EVAL_OOM_RETURN_IF_NULL(ctx, current_c, false);

        if (!copy_path_is_symlink(current_c)) {
            String_View base = copy_basename_sv(current);
            String_View dst = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, base);
            char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
            EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);

            bool src_is_dir = false;
            if (!copy_copy_entry_raw(current_c, dst_c, &src_is_dir)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to copy entry"), current);
                return false;
            }

#if defined(_WIN32)
            for (size_t i = 0; i < arena_arr_len(link_names); i++) {
                if (eval_sv_key_eq(link_names[i], base)) continue;
                String_View alias_dst = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, link_names[i]);
                char *alias_dst_c = eval_sv_to_cstr_temp(ctx, alias_dst);
                EVAL_OOM_RETURN_IF_NULL(ctx, alias_dst_c, false);

                bool alias_ok = src_is_dir ? nob_copy_directory_recursively(dst_c, alias_dst_c)
                                           : nob_copy_file(dst_c, alias_dst_c);
                if (!alias_ok) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to materialize FOLLOW_SYMLINK_CHAIN alias on Windows"), alias_dst);
                    return false;
                }
            }
#endif

            mode_t mode = 0;
            if (perms && copy_permissions_pick_mode(perms, src_is_dir, &mode)) {
                if (!copy_apply_permissions(dst_c, mode)) {
                    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) copied entry but failed to apply permissions"), dst);
                } else if (io_applied_any_permissions) {
                    *io_applied_any_permissions = true;
                }
            }

            return true;
        }

        String_View target = nob_sv_from_cstr("");
        if (!copy_read_symlink_target_temp(ctx, current_c, &target) || target.count == 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to resolve symlink target in FOLLOW_SYMLINK_CHAIN"), current);
            return false;
        }

        String_View link_name = copy_basename_sv(current);
        String_View dst_link = eval_sv_path_join(eval_temp_arena(ctx), dest_dir, link_name);
        char *dst_link_c = eval_sv_to_cstr_temp(ctx, dst_link);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_link_c, false);

#if !defined(_WIN32)
        char *target_c = eval_sv_to_cstr_temp(ctx, target);
        EVAL_OOM_RETURN_IF_NULL(ctx, target_c, false);
        if (!copy_create_symlink_like(target_c, dst_link_c)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to recreate symlink in FOLLOW_SYMLINK_CHAIN"), dst_link);
            return false;
        }
#else
        if (!svu_list_push_temp(ctx, &link_names, link_name)) return false;
#endif

        if (eval_sv_is_abs_path(target)) {
            current = eval_file_cmk_path_normalize_temp(ctx, target);
        } else {
            String_View parent = svu_dirname(current);
            current = eval_file_cmk_path_normalize_temp(ctx, eval_sv_path_join(eval_temp_arena(ctx), parent, target));
        }
        if (eval_should_stop(ctx) || current.count == 0) return false;
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_OUT_OF_RANGE, "eval_file", nob_sv_from_cstr("file(COPY) FOLLOW_SYMLINK_CHAIN exceeded maximum link depth"), src);
    return false;
}

static bool copy_filter_matches(EvalExecContext *ctx, Copy_Filter *f, String_View src, String_View base) {
    if (!ctx || !f) return false;
    if (!f->is_regex) {
        return eval_file_glob_match_sv(f->expr, base, false) || eval_file_glob_match_sv(f->expr, src, false);
    }
    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    if (!src_c) return false;
    return regexec(&f->regex, src_c, 0, NULL, 0) == 0;
}

static bool copy_should_include(EvalExecContext *ctx,
                                Copy_Filter *filters,
                                size_t filter_count,
                                bool files_matching,
                                String_View src,
                                String_View base) {
    bool include = !files_matching;
    for (size_t i = 0; i < filter_count; i++) {
        Copy_Filter *f = &filters[i];
        if (!copy_filter_matches(ctx, f, src, base)) continue;
        if (f->exclude) include = false;
        else include = true;
    }
    return include;
}

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    SV_List args;
    bool files_matching;
    bool follow_symlink_chain;
    bool saw_follow_symlink_option;
    bool saw_unknown_permission_token;
    Copy_Filter filters[64];
    size_t filter_count;
    Copy_Permissions perms;
} Copy_Parse_State;

static bool copy_parse_on_option(EvalExecContext *ctx,
                                 void *userdata,
                                 int id,
                                 SV_List values,
                                 size_t token_index) {
    if (!ctx || !userdata) return false;
    Copy_Parse_State *st = (Copy_Parse_State*)userdata;
    String_View token = nob_sv_from_cstr("");
    if (token_index < arena_arr_len(st->args)) {
        token = st->args[token_index];
    }

    if (id == COPY_KEY_FILES_MATCHING) {
        st->files_matching = true;
        return true;
    }
    if (id == COPY_KEY_PATTERN || id == COPY_KEY_REGEX) {
        if (arena_arr_len(values) == 0) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("eval_file"), st->command_name, st->origin, nob_sv_from_cstr("file(COPY) missing argument after PATTERN/REGEX"), token);
            return false;
        }
        if (st->filter_count >= NOB_ARRAY_LEN(st->filters)) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNSUPPORTED_OPERATION, nob_sv_from_cstr("eval_file"), st->command_name, st->origin, nob_sv_from_cstr("file(COPY) filter limit reached; extra filters ignored"), token);
            return true;
        }
        st->filters[st->filter_count].is_regex = (id == COPY_KEY_REGEX);
        st->filters[st->filter_count].expr = values[0];
        st->filter_count++;
        return true;
    }
    if (id == COPY_KEY_EXCLUDE) {
        if (st->filter_count == 0) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, nob_sv_from_cstr("eval_file"), st->command_name, st->origin, nob_sv_from_cstr("file(COPY) EXCLUDE without a previous PATTERN/REGEX is ignored"), token);
        } else {
            st->filters[st->filter_count - 1].exclude = true;
        }
        return true;
    }
    if (id == COPY_KEY_FOLLOW_SYMLINK_CHAIN) {
        st->follow_symlink_chain = true;
        st->saw_follow_symlink_option = true;
        return true;
    }
    if (id == COPY_KEY_USE_SOURCE_PERMISSIONS) {
        st->perms.saw_use_source_permissions = true;
        return true;
    }
    if (id == COPY_KEY_NO_SOURCE_PERMISSIONS) {
        st->perms.saw_no_source_permissions = true;
        return true;
    }
    if (id == COPY_KEY_PERMISSIONS ||
        id == COPY_KEY_FILE_PERMISSIONS ||
        id == COPY_KEY_DIRECTORY_PERMISSIONS) {
        mode_t parsed_mode = 0;
        bool has_any = false;
        for (size_t i = 0; i < arena_arr_len(values); i++) {
            if (copy_permission_add_token(&parsed_mode, values[i])) {
                has_any = true;
            } else {
                st->saw_unknown_permission_token = true;
                EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, nob_sv_from_cstr("eval_file"), st->command_name, st->origin, nob_sv_from_cstr("file(COPY) unknown permission token"), values[i]);
            }
        }
        if (!has_any) {
            EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_file"), st->command_name, st->origin, nob_sv_from_cstr("file(COPY) permission list has no valid tokens"), token);
        } else if (id == COPY_KEY_PERMISSIONS) {
            st->perms.has_permissions = true;
            st->perms.permissions_mode = parsed_mode;
        } else if (id == COPY_KEY_FILE_PERMISSIONS) {
            st->perms.has_file_permissions = true;
            st->perms.file_permissions_mode = parsed_mode;
        } else if (id == COPY_KEY_DIRECTORY_PERMISSIONS) {
            st->perms.has_directory_permissions = true;
            st->perms.directory_permissions_mode = parsed_mode;
        }
        return true;
    }

    return true;
}

static bool copy_parse_on_positional(EvalExecContext *ctx,
                                     void *userdata,
                                     String_View value,
                                     size_t token_index) {
    (void)ctx;
    (void)userdata;
    (void)value;
    (void)token_index;
    return true;
}

void eval_file_handle_copy(EvalExecContext *ctx, const Node *node, SV_List args) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(args) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(COPY) requires sources and DESTINATION"), nob_sv_from_cstr("Usage: file(COPY <src>... DESTINATION <dir>)"));
        return;
    }

    size_t dest_idx = SIZE_MAX;
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "DESTINATION")) {
            dest_idx = i;
            break;
        }
    }
    if (dest_idx == SIZE_MAX || dest_idx + 1 >= arena_arr_len(args) || dest_idx == 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "eval_file", nob_sv_from_cstr("file(COPY) missing DESTINATION or sources"), nob_sv_from_cstr("Usage: file(COPY <src>... DESTINATION <dir>)"));
        return;
    }

    String_View dest = args[dest_idx + 1];
    if (!eval_sv_is_abs_path(dest)) {
        dest = eval_sv_path_join(eval_temp_arena(ctx), eval_current_binary_dir(ctx), dest);
    }

    static const Eval_Opt_Spec k_copy_specs[] = {
        EVAL_OPT_SPEC(COPY_KEY_FILES_MATCHING, "FILES_MATCHING", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(COPY_KEY_PATTERN, "PATTERN", EVAL_OPT_OPTIONAL_SINGLE),
        EVAL_OPT_SPEC(COPY_KEY_REGEX, "REGEX", EVAL_OPT_OPTIONAL_SINGLE),
        EVAL_OPT_SPEC(COPY_KEY_EXCLUDE, "EXCLUDE", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(COPY_KEY_FOLLOW_SYMLINK_CHAIN, "FOLLOW_SYMLINK_CHAIN", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(COPY_KEY_PERMISSIONS, "PERMISSIONS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(COPY_KEY_FILE_PERMISSIONS, "FILE_PERMISSIONS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(COPY_KEY_DIRECTORY_PERMISSIONS, "DIRECTORY_PERMISSIONS", EVAL_OPT_MULTI),
        EVAL_OPT_SPEC(COPY_KEY_USE_SOURCE_PERMISSIONS, "USE_SOURCE_PERMISSIONS", EVAL_OPT_FLAG),
        EVAL_OPT_SPEC(COPY_KEY_NO_SOURCE_PERMISSIONS, "NO_SOURCE_PERMISSIONS", EVAL_OPT_FLAG),
    };
    Copy_Parse_State parsed = {
        .origin = o,
        .command_name = node->as.cmd.name,
        .args = args,
        .files_matching = false,
        .follow_symlink_chain = false,
        .saw_follow_symlink_option = false,
        .saw_unknown_permission_token = false,
        .filter_count = 0,
        .perms = {0},
    };
    Eval_Opt_Parse_Config cfg = {
        .component = nob_sv_from_cstr("eval_file"),
        .command = node->as.cmd.name,
        .unknown_as_positional = true,
        .warn_unknown = false,
    };
    cfg.origin = o;
    if (!eval_opt_parse_walk(ctx,
                             args,
                             dest_idx + 2,
                             k_copy_specs,
                             NOB_ARRAY_LEN(k_copy_specs),
                             cfg,
                             copy_parse_on_option,
                             copy_parse_on_positional,
                             &parsed)) {
        return;
    }
    bool files_matching = parsed.files_matching;
    bool follow_symlink_chain = parsed.follow_symlink_chain;
    bool saw_unknown_permission_token = parsed.saw_unknown_permission_token;
    Copy_Filter *filters = parsed.filters;
    size_t filter_count = parsed.filter_count;
    Copy_Permissions perms = parsed.perms;

    for (size_t i = 0; i < filter_count; i++) {
        if (!filters[i].is_regex) continue;
        char *expr_c = eval_sv_to_cstr_temp(ctx, filters[i].expr);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, expr_c);
        if (regcomp(&filters[i].regex, expr_c, REG_EXTENDED) != 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "eval_file", nob_sv_from_cstr("file(COPY) invalid REGEX filter"), filters[i].expr);
            for (size_t j = 0; j < i; j++) {
                if (filters[j].is_regex && filters[j].regex_ready) regfree(&filters[j].regex);
            }
            return;
        }
        filters[i].regex_ready = true;
    }

    if (!eval_file_mkdir_p(ctx, dest)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to create destination"), dest);
        return;
    }

    bool applied_any_permissions = false;
    bool emitted_unsupported_replay_marker = false;
    bool replay_supported = !files_matching && filter_count == 0 && !follow_symlink_chain;
    for (size_t i = 1; i < dest_idx; i++) {
        String_View src = args[i];
        if (!eval_sv_is_abs_path(src)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), eval_current_source_dir(ctx), src);
        }

        char *src_c = eval_sv_to_cstr_temp(ctx, src);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, src_c);

        const char *base = src_c;
        for (const char *p = src_c; *p; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        String_View base_sv = nob_sv_from_cstr(base);
        if (!copy_should_include(ctx, filters, filter_count, files_matching, src, base_sv)) {
            continue;
        }
        String_View dst_sv = eval_sv_path_join(eval_temp_arena(ctx), dest, nob_sv_from_cstr(base));
        char *dst_c = eval_sv_to_cstr_temp(ctx, dst_sv);
        EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dst_c);

        if (follow_symlink_chain && copy_path_is_symlink(src_c)) {
            if (!copy_follow_symlink_chain(ctx, node, o, src, dest, &perms, &applied_any_permissions)) {
                return;
            }
            if (!emitted_unsupported_replay_marker) {
                (void)copy_emit_unsupported_replay_marker(ctx, o, nob_sv_from_cstr("file(COPY) FOLLOW_SYMLINK_CHAIN replay is not supported"));
                emitted_unsupported_replay_marker = true;
            }
            continue;
        }

        bool src_is_dir = false;
        if (!copy_copy_entry_raw(src_c, dst_c, &src_is_dir)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) failed to copy entry"), src);
            return;
        }

        if (perms.saw_use_source_permissions &&
            !perms.has_permissions &&
            !perms.has_file_permissions &&
            !perms.has_directory_permissions) {
#if !defined(_WIN32)
            struct stat src_st = {0};
            if (stat(src_c, &src_st) == 0) {
                if (copy_apply_permissions(dst_c, src_st.st_mode & 0777)) {
                    applied_any_permissions = true;
                }
            }
#endif
        }

        mode_t mode = 0;
        if (copy_permissions_pick_mode(&perms, src_is_dir, &mode)) {
            if (!copy_apply_permissions(dst_c, mode)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) copied entry but failed to apply permissions"), dst_sv);
            } else {
                applied_any_permissions = true;
            }
        }

        if (replay_supported) {
            (void)copy_emit_replay(ctx,
                                   o,
                                   src,
                                   dst_sv,
                                   src_is_dir,
                                   &perms,
                                   perms.saw_use_source_permissions &&
                                       !perms.has_permissions &&
                                       !perms.has_file_permissions &&
                                       !perms.has_directory_permissions,
                                   src_c);
        } else if (!emitted_unsupported_replay_marker) {
            (void)copy_emit_unsupported_replay_marker(ctx, o, nob_sv_from_cstr("file(COPY) filtered replay is not supported"));
            emitted_unsupported_replay_marker = true;
        }
    }

    if ((perms.has_permissions || perms.has_file_permissions || perms.has_directory_permissions) &&
        !applied_any_permissions &&
        !saw_unknown_permission_token) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, "eval_file", nob_sv_from_cstr("file(COPY) permissions were requested but not applied"), nob_sv_from_cstr("Check destination type and platform permission support"));
    }
    for (size_t i = 0; i < filter_count; i++) {
        if (filters[i].is_regex && filters[i].regex_ready) regfree(&filters[i].regex);
    }
}
