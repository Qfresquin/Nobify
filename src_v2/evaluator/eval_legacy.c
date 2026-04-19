#include "eval_legacy.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <stdio.h>
#include <string.h>

typedef struct Write_File_Request Write_File_Request;

static bool legacy_emit_diag(EvalExecContext *ctx,
                             const Node *node,
                             Cmake_Diag_Severity severity,
                             String_View cause,
                             String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("eval_legacy"), node ? node->as.cmd.name : nob_sv_from_cstr(""), node ? eval_origin_from_node(ctx, node) : (Cmake_Event_Origin){0}, cause, hint);
}

static String_View legacy_current_binary_dir(EvalExecContext *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View legacy_resolve_binary_path(EvalExecContext *ctx, String_View raw) {
    return eval_path_resolve_for_cmake_arg(ctx, raw, legacy_current_binary_dir(ctx), true);
}

static bool legacy_emit_install_rule(EvalExecContext *ctx,
                                     const Node *node,
                                     Cmake_Install_Rule_Type rule_type,
                                     String_View item,
                                     String_View destination) {
    return eval_emit_install_rule_add(ctx,
                                      eval_origin_from_node(ctx, node),
                                      rule_type,
                                      item,
                                      destination,
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""),
                                      nob_sv_from_cstr(""));
}

static String_View legacy_stem_from_path(String_View path) {
    size_t start = 0;
    size_t end = path.count;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
    }
    for (size_t i = start; i < path.count; i++) {
        if (path.data[i] == '.') {
            end = i;
            break;
        }
    }
    if (end < start) end = start;
    return nob_sv_from_parts(path.data + start, end - start);
}

static String_View legacy_generated_name_temp(EvalExecContext *ctx,
                                              const char *prefix,
                                              String_View stem,
                                              const char *suffix) {
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + stem.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    if (stem.count > 0) memcpy(buf + prefix_len, stem.data, stem.count);
    memcpy(buf + prefix_len + stem.count, suffix, suffix_len);
    buf[prefix_len + stem.count + suffix_len] = '\0';
    return nob_sv_from_parts(buf, prefix_len + stem.count + suffix_len);
}

static bool legacy_emit_replay_make_directory(EvalExecContext *ctx,
                                              const Node *node,
                                              const SV_List *resolved_dirs) {
    String_View action_key = nob_sv_from_cstr("");
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_FILESYSTEM,
                                  EVENT_REPLAY_OPCODE_FS_MKDIR,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(*resolved_dirs); ++i) {
        if (!eval_emit_replay_action_add_output(ctx, origin, action_key, (*resolved_dirs)[i])) return false;
    }
    return true;
}

static bool legacy_metadata_only(EvalExecContext *ctx,
                                 const Node *node,
                                 size_t min_args,
                                 String_View command_name) {
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(a) < min_args) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("legacy command received too few arguments"),
                               command_name);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    return eval_legacy_publish_args(ctx, command_name, &a);
}

Eval_Result eval_handle_export_library_dependencies(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("export_library_dependencies"))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_load_command(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("load_command"))) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_output_required_files(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 2, nob_sv_from_cstr("output_required_files"))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_subdir_depends(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 2, nob_sv_from_cstr("subdir_depends"))) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_subdirs(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("subdirs"))) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_use_mangled_mesa(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 0, nob_sv_from_cstr("use_mangled_mesa"))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_utility_source(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 3, nob_sv_from_cstr("utility_source"))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_variable_requires(EvalExecContext *ctx, const Node *node) {
    if (!legacy_metadata_only(ctx, node, 3, nob_sv_from_cstr("variable_requires"))) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

typedef struct {
    SV_List directories;
} Make_Directory_Request;

struct Write_File_Request {
    String_View path;
    String_View contents;
    bool append;
};

static bool legacy_emit_replay_write_file(EvalExecContext *ctx,
                                          const Node *node,
                                          const Write_File_Request *req) {
    String_View action_key = nob_sv_from_cstr("");
    Event_Replay_Opcode opcode = EVENT_REPLAY_OPCODE_FS_WRITE_TEXT;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!ctx || !req) return false;
    if (req->append) opcode = EVENT_REPLAY_OPCODE_FS_APPEND_TEXT;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_FILESYSTEM,
                                  opcode,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, req->path) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, req->contents)) {
        return false;
    }
    if (opcode == EVENT_REPLAY_OPCODE_FS_WRITE_TEXT) {
        if (!eval_emit_replay_action_add_argv(ctx, origin, action_key, 1, nob_sv_from_cstr(""))) return false;
    }
    return true;
}

typedef struct {
    String_View variable;
    SV_List values;
} Remove_Request;

typedef struct {
    String_View output_var;
    SV_List headers;
} Qt_Wrap_Cpp_Request;

typedef struct {
    String_View headers_var;
    String_View sources_var;
    SV_List ui_files;
} Qt_Wrap_Ui_Request;

typedef struct {
    String_View target_name;
    SV_List ui_files;
} Fltk_Wrap_Ui_Request;

typedef struct {
    String_View variable;
    String_View command;
} Variable_Watch_Request;

static bool legacy_collect_temp_args(EvalExecContext *ctx,
                                     SV_List args,
                                     size_t begin,
                                     SV_List *out) {
    if (!ctx || !out) return false;
    *out = NULL;
    for (size_t i = begin; i < arena_arr_len(args); i++) {
        if (!svu_list_push_temp(ctx, out, args[i])) return false;
    }
    return true;
}

static bool legacy_parse_make_directory_request(EvalExecContext *ctx,
                                                const Node *node,
                                                SV_List args,
                                                Make_Directory_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) == 0) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("make_directory() requires at least one directory"),
                               nob_sv_from_cstr("Usage: make_directory(<dir>...)"));
        return false;
    }
    return legacy_collect_temp_args(ctx, args, 0, &out_req->directories);
}

static bool legacy_execute_make_directory_request(EvalExecContext *ctx,
                                                  const Node *node,
                                                  const Make_Directory_Request *req) {
    SV_List resolved_dirs = NULL;
    if (!ctx || !node || !req) return false;
    for (size_t i = 0; i < arena_arr_len(req->directories); i++) {
        String_View path = legacy_resolve_binary_path(ctx, req->directories[i]);
        if (eval_should_stop(ctx)) return false;
        if (!eval_mkdirs_for_parent(ctx, path)) return false;
        char *path_c = eval_sv_to_cstr_temp(ctx, path);
        EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
        if (!nob_mkdir_if_not_exists(path_c)) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("make_directory() failed to create a directory"),
                                   path);
        }
        if (!svu_list_push_temp(ctx, &resolved_dirs, path)) return false;
    }
    (void)legacy_emit_replay_make_directory(ctx, node, &resolved_dirs);
    return true;
}

static bool legacy_parse_write_file_request(EvalExecContext *ctx,
                                            const Node *node,
                                            SV_List args,
                                            Write_File_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Write_File_Request){0};

    if (arena_arr_len(args) < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("write_file() requires a path and at least one content argument"),
                               nob_sv_from_cstr("Usage: write_file(<file> <content>... [APPEND])"));
        return false;
    }

    size_t content_end = arena_arr_len(args);
    if (eval_sv_eq_ci_lit(args[arena_arr_len(args) - 1], "APPEND")) {
        out_req->append = true;
        content_end--;
        if (content_end < 2) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("write_file(APPEND) still requires content"),
                                   nob_sv_from_cstr("Usage: write_file(<file> <content>... [APPEND])"));
            return false;
        }
    }

    out_req->path = legacy_resolve_binary_path(ctx, args[0]);
    if (eval_should_stop(ctx)) return false;
    size_t content_count = content_end - 1;
    out_req->contents = (content_count == 1) ? args[1] : svu_join_no_sep_temp(ctx, &args[1], content_count);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool legacy_execute_write_file_request(EvalExecContext *ctx,
                                              const Node *node,
                                              const Write_File_Request *req) {
    if (!ctx || !node || !req) return false;
    if (!eval_write_text_file(ctx, req->path, req->contents, req->append)) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("write_file() failed to write the requested file"),
                               req->path);
    }
    (void)legacy_emit_replay_write_file(ctx, node, req);
    return true;
}

static bool legacy_parse_remove_request(EvalExecContext *ctx,
                                        const Node *node,
                                        SV_List args,
                                        Remove_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Remove_Request){0};
    if (arena_arr_len(args) < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("remove() requires a variable and one or more values"),
                               nob_sv_from_cstr("Usage: remove(<variable> <value>...)"));
        return false;
    }
    out_req->variable = args[0];
    return legacy_collect_temp_args(ctx, args, 1, &out_req->values);
}

static bool legacy_execute_remove_request(EvalExecContext *ctx,
                                          const Remove_Request *req) {
    if (!ctx || !req) return false;

    String_View current = eval_var_get_visible(ctx, req->variable);
    SV_List items = NULL;
    if (current.count > 0 && !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), current, &items)) {
        return false;
    }

    SV_List kept = NULL;
    for (size_t i = 0; i < arena_arr_len(items); i++) {
        bool drop = false;
        for (size_t j = 0; j < arena_arr_len(req->values); j++) {
            if (nob_sv_eq(items[i], req->values[j])) {
                drop = true;
                break;
            }
        }
        if (!drop && !svu_list_push_temp(ctx, &kept, items[i])) return false;
    }

    return eval_var_set_current(ctx, req->variable, eval_sv_join_semi_temp(ctx, kept, arena_arr_len(kept)));
}

Eval_Result eval_handle_install_files(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 3) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_files() requires a destination and one or more files"),
                               nob_sv_from_cstr("Usage: install_files(<dir> <extension> <file>...)"));
        return eval_result_from_ctx(ctx);
    }
    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_FILE, a[i], a[0])) return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_install_programs(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_programs() requires a destination and one or more files"),
                               nob_sv_from_cstr("Usage: install_programs(<dir> <file>...)"));
        return eval_result_from_ctx(ctx);
    }
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_PROGRAM, a[i], a[0])) return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_install_targets(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_targets() requires a destination and one or more targets"),
                               nob_sv_from_cstr("Usage: install_targets(<dir> <target>...)"));
        return eval_result_from_ctx(ctx);
    }
    size_t start = 1;
    if (arena_arr_len(a) >= 4 && eval_sv_eq_ci_lit(a[1], "RUNTIME_DIRECTORY")) {
        start = 3;
    }
    if (start >= arena_arr_len(a)) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_targets() requires one or more target names"),
                               nob_sv_from_cstr("Usage: install_targets(<dir> [RUNTIME_DIRECTORY <dir>] <target>...)"));
        return eval_result_from_ctx(ctx);
    }
    for (size_t i = start; i < arena_arr_len(a); i++) {
        if (!eval_target_known(ctx, a[i])) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("install_targets() target was not declared"),
                                   a[i]);
            continue;
        }
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_TARGET, a[i], a[0])) return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

static bool legacy_parse_qt_wrap_cpp_request(EvalExecContext *ctx,
                                             const Node *node,
                                             SV_List args,
                                             Qt_Wrap_Cpp_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Qt_Wrap_Cpp_Request){0};
    if (arena_arr_len(args) < 3) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("qt_wrap_cpp() requires a library, output variable and one or more headers"),
                               nob_sv_from_cstr("Usage: qt_wrap_cpp(<lib> <out-var> <header>...)"));
        return false;
    }
    out_req->output_var = args[1];
    return legacy_collect_temp_args(ctx, args, 2, &out_req->headers);
}

static bool legacy_execute_qt_wrap_cpp_request(EvalExecContext *ctx,
                                               const Qt_Wrap_Cpp_Request *req) {
    if (!ctx || !req) return false;
    SV_List generated = NULL;
    for (size_t i = 0; i < arena_arr_len(req->headers); i++) {
        String_View stem = legacy_stem_from_path(req->headers[i]);
        if (!svu_list_push_temp(ctx, &generated, legacy_generated_name_temp(ctx, "moc_", stem, ".cxx"))) {
            return false;
        }
    }
    return eval_var_set_current(ctx, req->output_var, eval_sv_join_semi_temp(ctx, generated, arena_arr_len(generated)));
}

static bool legacy_parse_qt_wrap_ui_request(EvalExecContext *ctx,
                                            const Node *node,
                                            SV_List args,
                                            Qt_Wrap_Ui_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Qt_Wrap_Ui_Request){0};
    if (arena_arr_len(args) < 4) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("qt_wrap_ui() requires a library, header var, source var and one or more UI files"),
                               nob_sv_from_cstr("Usage: qt_wrap_ui(<lib> <headers-var> <sources-var> <ui>...)"));
        return false;
    }
    out_req->headers_var = args[1];
    out_req->sources_var = args[2];
    return legacy_collect_temp_args(ctx, args, 3, &out_req->ui_files);
}

static bool legacy_execute_qt_wrap_ui_request(EvalExecContext *ctx,
                                              const Qt_Wrap_Ui_Request *req) {
    if (!ctx || !req) return false;
    SV_List headers = NULL;
    SV_List sources = NULL;
    for (size_t i = 0; i < arena_arr_len(req->ui_files); i++) {
        String_View stem = legacy_stem_from_path(req->ui_files[i]);
        if (!svu_list_push_temp(ctx, &headers, legacy_generated_name_temp(ctx, "ui_", stem, ".h"))) {
            return false;
        }
        if (!svu_list_push_temp(ctx, &sources, legacy_generated_name_temp(ctx, "ui_", stem, ".cxx"))) {
            return false;
        }
    }
    if (!eval_var_set_current(ctx, req->headers_var, eval_sv_join_semi_temp(ctx, headers, arena_arr_len(headers)))) {
        return false;
    }
    return eval_var_set_current(ctx, req->sources_var, eval_sv_join_semi_temp(ctx, sources, arena_arr_len(sources)));
}

static bool legacy_parse_fltk_wrap_ui_request(EvalExecContext *ctx,
                                              const Node *node,
                                              SV_List args,
                                              Fltk_Wrap_Ui_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Fltk_Wrap_Ui_Request){0};
    if (arena_arr_len(args) < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("fltk_wrap_ui() requires a target name and one or more UI files"),
                               nob_sv_from_cstr("Usage: fltk_wrap_ui(<lib> <ui>...)"));
        return false;
    }
    out_req->target_name = args[0];
    return legacy_collect_temp_args(ctx, args, 1, &out_req->ui_files);
}

static bool legacy_execute_fltk_wrap_ui_request(EvalExecContext *ctx,
                                                const Fltk_Wrap_Ui_Request *req) {
    if (!ctx || !req) return false;

    SV_List outputs = NULL;
    for (size_t i = 0; i < arena_arr_len(req->ui_files); i++) {
        String_View stem = legacy_stem_from_path(req->ui_files[i]);
        if (!svu_list_push_temp(ctx, &outputs, legacy_generated_name_temp(ctx, "fluid_", stem, ".cxx"))) {
            return false;
        }
        if (!svu_list_push_temp(ctx, &outputs, legacy_generated_name_temp(ctx, "fluid_", stem, ".h"))) {
            return false;
        }
    }

    size_t total = req->target_name.count + sizeof("_FLTK_UI_SRCS") - 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, req->target_name.data, req->target_name.count);
    memcpy(buf + req->target_name.count, "_FLTK_UI_SRCS", sizeof("_FLTK_UI_SRCS"));
    return eval_var_set_current(ctx,
                                nob_sv_from_parts(buf, total),
                                eval_sv_join_semi_temp(ctx, outputs, arena_arr_len(outputs)));
}

static bool legacy_parse_variable_watch_request(EvalExecContext *ctx,
                                                const Node *node,
                                                SV_List args,
                                                Variable_Watch_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    *out_req = (Variable_Watch_Request){0};
    if (arena_arr_len(args) == 0 || arena_arr_len(args) > 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("variable_watch() requires a variable and an optional command"),
                               nob_sv_from_cstr("Usage: variable_watch(<var> [command])"));
        return false;
    }
    out_req->variable = args[0];
    out_req->command = arena_arr_len(args) > 1 ? args[1] : nob_sv_from_cstr("");
    return true;
}

static bool legacy_execute_variable_watch_request(EvalExecContext *ctx,
                                                  const Variable_Watch_Request *req) {
    if (!ctx || !req) return false;

    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->watched_variables); i++) {
        if (!eval_sv_key_eq(commands->watched_variables[i], req->variable)) continue;
        if (i < arena_arr_len(commands->watched_variable_commands)) {
            commands->watched_variable_commands[i] = sv_copy_to_event_arena(ctx, req->command);
        }
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    String_View stable_var = sv_copy_to_event_arena(ctx, req->variable);
    String_View stable_cmd = sv_copy_to_event_arena(ctx, req->command);
    if (eval_should_stop(ctx)) return false;
    if (!EVAL_ARR_PUSH(ctx, commands->user_commands_arena, commands->watched_variables, stable_var)) return false;
    if (!EVAL_ARR_PUSH(ctx, commands->user_commands_arena, commands->watched_variable_commands, stable_cmd)) return false;
    return true;
}

Eval_Result eval_handle_make_directory(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Make_Directory_Request req = {0};
    if (!legacy_parse_make_directory_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_make_directory_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_write_file(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Write_File_Request req = {0};
    if (!legacy_parse_write_file_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_write_file_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_remove(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Remove_Request req = {0};
    if (!legacy_parse_remove_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_remove_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_qt_wrap_cpp(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Qt_Wrap_Cpp_Request req = {0};
    if (!legacy_parse_qt_wrap_cpp_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_qt_wrap_cpp_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_qt_wrap_ui(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Qt_Wrap_Ui_Request req = {0};
    if (!legacy_parse_qt_wrap_ui_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_qt_wrap_ui_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fltk_wrap_ui(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Fltk_Wrap_Ui_Request req = {0};
    if (!legacy_parse_fltk_wrap_ui_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_fltk_wrap_ui_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_variable_watch(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Variable_Watch_Request req = {0};
    if (!legacy_parse_variable_watch_request(ctx, node, args, &req)) return eval_result_from_ctx(ctx);
    if (!legacy_execute_variable_watch_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
