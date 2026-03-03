#include "eval_legacy.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <stdio.h>
#include <string.h>

static bool legacy_emit_diag(Evaluator_Context *ctx,
                             const Node *node,
                             Cmake_Diag_Severity severity,
                             String_View cause,
                             String_View hint) {
    return eval_emit_diag(ctx,
                          severity,
                          nob_sv_from_cstr("eval_legacy"),
                          node ? node->as.cmd.name : nob_sv_from_cstr(""),
                          node ? eval_origin_from_node(ctx, node) : (Cmake_Event_Origin){0},
                          cause,
                          hint);
}

static String_View legacy_current_binary_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static String_View legacy_resolve_binary_path(Evaluator_Context *ctx, String_View raw) {
    return eval_path_resolve_for_cmake_arg(ctx, raw, legacy_current_binary_dir(ctx), true);
}

static bool legacy_emit_install_rule(Evaluator_Context *ctx,
                                     const Node *node,
                                     Cmake_Install_Rule_Type rule_type,
                                     String_View item,
                                     String_View destination) {
    Cmake_Event ev = {0};
    ev.kind = EV_INSTALL_ADD_RULE;
    ev.origin = eval_origin_from_node(ctx, node);
    ev.as.install_add_rule.rule_type = rule_type;
    ev.as.install_add_rule.item = sv_copy_to_event_arena(ctx, item);
    ev.as.install_add_rule.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
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

static String_View legacy_generated_name_temp(Evaluator_Context *ctx,
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

static bool legacy_metadata_only(Evaluator_Context *ctx,
                                 const Node *node,
                                 size_t min_args,
                                 String_View command_name) {
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < min_args) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("legacy command received too few arguments"),
                               command_name);
        return !eval_should_stop(ctx);
    }
    return eval_legacy_publish_args(ctx, command_name, &a);
}

bool eval_handle_export_library_dependencies(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("export_library_dependencies"));
}

bool eval_handle_load_command(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("load_command"));
}

bool eval_handle_output_required_files(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 2, nob_sv_from_cstr("output_required_files"));
}

bool eval_handle_subdir_depends(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 2, nob_sv_from_cstr("subdir_depends"));
}

bool eval_handle_subdirs(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 1, nob_sv_from_cstr("subdirs"));
}

bool eval_handle_use_mangled_mesa(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 0, nob_sv_from_cstr("use_mangled_mesa"));
}

bool eval_handle_utility_source(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 3, nob_sv_from_cstr("utility_source"));
}

bool eval_handle_variable_requires(Evaluator_Context *ctx, const Node *node) {
    return legacy_metadata_only(ctx, node, 3, nob_sv_from_cstr("variable_requires"));
}

bool eval_handle_make_directory(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count == 0) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("make_directory() requires at least one directory"),
                               nob_sv_from_cstr("Usage: make_directory(<dir>...)"));
        return !eval_should_stop(ctx);
    }
    for (size_t i = 0; i < a.count; i++) {
        String_View path = legacy_resolve_binary_path(ctx, a.items[i]);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        if (!eval_mkdirs_for_parent(ctx, path)) return !eval_should_stop(ctx);
        char *path_c = eval_sv_to_cstr_temp(ctx, path);
        EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
        if (!nob_mkdir_if_not_exists(path_c)) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("make_directory() failed to create a directory"),
                                   path);
        }
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_write_file(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("write_file() requires a path and at least one content argument"),
                               nob_sv_from_cstr("Usage: write_file(<file> <content>... [APPEND])"));
        return !eval_should_stop(ctx);
    }

    bool append = false;
    size_t content_end = a.count;
    if (eval_sv_eq_ci_lit(a.items[a.count - 1], "APPEND")) {
        append = true;
        content_end--;
        if (content_end < 2) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("write_file(APPEND) still requires content"),
                                   nob_sv_from_cstr("Usage: write_file(<file> <content>... [APPEND])"));
            return !eval_should_stop(ctx);
        }
    }

    Nob_String_Builder sb = {0};
    for (size_t i = 1; i < content_end; i++) {
        nob_sb_append_buf(&sb, a.items[i].data, a.items[i].count);
    }
    String_View path = legacy_resolve_binary_path(ctx, a.items[0]);
    if (!eval_write_text_file(ctx, path, nob_sv_from_parts(sb.items, sb.count), append)) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("write_file() failed to write the requested file"),
                               path);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_remove(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("remove() requires a variable and one or more values"),
                               nob_sv_from_cstr("Usage: remove(<variable> <value>...)"));
        return !eval_should_stop(ctx);
    }

    String_View current = eval_var_get(ctx, a.items[0]);
    SV_List items = {0};
    if (current.count > 0 && !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), current, &items)) {
        return !eval_should_stop(ctx);
    }

    SV_List kept = {0};
    for (size_t i = 0; i < items.count; i++) {
        bool drop = false;
        for (size_t j = 1; j < a.count; j++) {
            if (nob_sv_eq(items.items[i], a.items[j])) {
                drop = true;
                break;
            }
        }
        if (!drop && !svu_list_push_temp(ctx, &kept, items.items[i])) return !eval_should_stop(ctx);
    }

    if (!eval_var_set(ctx, a.items[0], eval_sv_join_semi_temp(ctx, kept.items, kept.count))) {
        return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_install_files(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 3) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_files() requires a destination and one or more files"),
                               nob_sv_from_cstr("Usage: install_files(<dir> <extension> <file>...)"));
        return !eval_should_stop(ctx);
    }
    for (size_t i = 2; i < a.count; i++) {
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_FILE, a.items[i], a.items[0])) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_install_programs(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_programs() requires a destination and one or more files"),
                               nob_sv_from_cstr("Usage: install_programs(<dir> <file>...)"));
        return !eval_should_stop(ctx);
    }
    for (size_t i = 1; i < a.count; i++) {
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_PROGRAM, a.items[i], a.items[0])) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_install_targets(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_targets() requires a destination and one or more targets"),
                               nob_sv_from_cstr("Usage: install_targets(<dir> <target>...)"));
        return !eval_should_stop(ctx);
    }
    size_t start = 1;
    if (a.count >= 4 && eval_sv_eq_ci_lit(a.items[1], "RUNTIME_DIRECTORY")) {
        start = 3;
    }
    if (start >= a.count) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("install_targets() requires one or more target names"),
                               nob_sv_from_cstr("Usage: install_targets(<dir> [RUNTIME_DIRECTORY <dir>] <target>...)"));
        return !eval_should_stop(ctx);
    }
    for (size_t i = start; i < a.count; i++) {
        if (!eval_target_known(ctx, a.items[i])) {
            (void)legacy_emit_diag(ctx,
                                   node,
                                   EV_DIAG_ERROR,
                                   nob_sv_from_cstr("install_targets() target was not declared"),
                                   a.items[i]);
            continue;
        }
        if (!legacy_emit_install_rule(ctx, node, EV_INSTALL_RULE_TARGET, a.items[i], a.items[0])) return false;
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_qt_wrap_cpp(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 3) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("qt_wrap_cpp() requires a library, output variable and one or more headers"),
                               nob_sv_from_cstr("Usage: qt_wrap_cpp(<lib> <out-var> <header>...)"));
        return !eval_should_stop(ctx);
    }
    SV_List generated = {0};
    for (size_t i = 2; i < a.count; i++) {
        String_View stem = legacy_stem_from_path(a.items[i]);
        if (!svu_list_push_temp(ctx, &generated, legacy_generated_name_temp(ctx, "moc_", stem, ".cxx"))) {
            return !eval_should_stop(ctx);
        }
    }
    if (!eval_var_set(ctx, a.items[1], eval_sv_join_semi_temp(ctx, generated.items, generated.count))) {
        return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_qt_wrap_ui(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 4) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("qt_wrap_ui() requires a library, header var, source var and one or more UI files"),
                               nob_sv_from_cstr("Usage: qt_wrap_ui(<lib> <headers-var> <sources-var> <ui>...)"));
        return !eval_should_stop(ctx);
    }
    SV_List headers = {0};
    SV_List sources = {0};
    for (size_t i = 3; i < a.count; i++) {
        String_View stem = legacy_stem_from_path(a.items[i]);
        if (!svu_list_push_temp(ctx, &headers, legacy_generated_name_temp(ctx, "ui_", stem, ".h"))) {
            return !eval_should_stop(ctx);
        }
        if (!svu_list_push_temp(ctx, &sources, legacy_generated_name_temp(ctx, "ui_", stem, ".cxx"))) {
            return !eval_should_stop(ctx);
        }
    }
    if (!eval_var_set(ctx, a.items[1], eval_sv_join_semi_temp(ctx, headers.items, headers.count))) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, a.items[2], eval_sv_join_semi_temp(ctx, sources.items, sources.count))) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_fltk_wrap_ui(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("fltk_wrap_ui() requires a target name and one or more UI files"),
                               nob_sv_from_cstr("Usage: fltk_wrap_ui(<lib> <ui>...)"));
        return !eval_should_stop(ctx);
    }

    SV_List outputs = {0};
    for (size_t i = 1; i < a.count; i++) {
        String_View stem = legacy_stem_from_path(a.items[i]);
        if (!svu_list_push_temp(ctx, &outputs, legacy_generated_name_temp(ctx, "fluid_", stem, ".cxx"))) {
            return !eval_should_stop(ctx);
        }
        if (!svu_list_push_temp(ctx, &outputs, legacy_generated_name_temp(ctx, "fluid_", stem, ".h"))) {
            return !eval_should_stop(ctx);
        }
    }

    size_t total = a.items[0].count + sizeof("_FLTK_UI_SRCS") - 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, a.items[0].data, a.items[0].count);
    memcpy(buf + a.items[0].count, "_FLTK_UI_SRCS", sizeof("_FLTK_UI_SRCS"));
    if (!eval_var_set(ctx, nob_sv_from_parts(buf, total), eval_sv_join_semi_temp(ctx, outputs.items, outputs.count))) {
        return !eval_should_stop(ctx);
    }
    return !eval_should_stop(ctx);
}

bool eval_handle_variable_watch(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count == 0 || a.count > 2) {
        (void)legacy_emit_diag(ctx,
                               node,
                               EV_DIAG_ERROR,
                               nob_sv_from_cstr("variable_watch() requires a variable and an optional command"),
                               nob_sv_from_cstr("Usage: variable_watch(<var> [command])"));
        return !eval_should_stop(ctx);
    }

    for (size_t i = 0; i < ctx->watched_variables.count; i++) {
        if (!eval_sv_key_eq(ctx->watched_variables.items[i], a.items[0])) continue;
        if (i < ctx->watched_variable_commands.count) {
            ctx->watched_variable_commands.items[i] = sv_copy_to_event_arena(ctx, a.count > 1 ? a.items[1] : nob_sv_from_cstr(""));
        }
        return !eval_should_stop(ctx);
    }

    String_View stable_var = sv_copy_to_event_arena(ctx, a.items[0]);
    String_View stable_cmd = sv_copy_to_event_arena(ctx, a.count > 1 ? a.items[1] : nob_sv_from_cstr(""));
    if (eval_should_stop(ctx)) return false;
    if (!arena_da_try_append(ctx->known_targets_arena, &ctx->watched_variables, stable_var)) return ctx_oom(ctx);
    if (!arena_da_try_append(ctx->known_targets_arena, &ctx->watched_variable_commands, stable_cmd)) return ctx_oom(ctx);
    return !eval_should_stop(ctx);
}

