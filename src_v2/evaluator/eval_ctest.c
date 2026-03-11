#include "eval_ctest.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "tinydir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    String_View key;
    String_View value;
} Ctest_Parsed_Field;

typedef struct {
    SV_List positionals;
    Ctest_Parsed_Field *fields;
} Ctest_Parse_Result;

typedef struct {
    const char *const *single_keywords;
    size_t single_count;
    const char *const *multi_keywords;
    size_t multi_count;
    const char *const *flag_keywords;
    size_t flag_count;
    size_t min_positionals;
    size_t max_positionals;
} Ctest_Parse_Spec;

typedef struct {
    size_t message_check_count;
    size_t user_commands_count;
    size_t known_targets_count;
    size_t alias_targets_count;
    String_View dependency_provider_command_name;
    bool dependency_provider_supports_find_package;
    bool dependency_provider_supports_fetchcontent_makeavailable_serial;
    size_t dependency_provider_active_find_package_depth;
    size_t dependency_provider_active_fetchcontent_makeavailable_depth;
    size_t active_find_packages_count;
    size_t fetchcontent_declarations_count;
    size_t fetchcontent_states_count;
    size_t active_fetchcontent_makeavailable_count;
    size_t watched_variables_count;
    size_t watched_variable_commands_count;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
    bool scope_pushed;
} Ctest_New_Process_State;

static String_View ctest_current_binary_dir(Evaluator_Context *ctx);
static String_View ctest_binary_root(Evaluator_Context *ctx);
static size_t s_ctest_session_tag_counter = 0;

static bool ctest_emit_diag(Evaluator_Context *ctx,
                            const Node *node,
                            Cmake_Diag_Severity severity,
                            String_View cause,
                            String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_ctest"), node->as.cmd.name, eval_origin_from_node(ctx, node), cause, hint);
}

static bool ctest_set_field(Evaluator_Context *ctx,
                            String_View command_name,
                            const char *field_name,
                            String_View value) {
    if (!ctx || !field_name) return false;
    size_t total = sizeof("NOBIFY_CTEST::") - 1 + command_name.count + sizeof("::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    int n = snprintf(buf,
                     total + 1,
                     "NOBIFY_CTEST::%.*s::%s",
                     (int)command_name.count,
                     command_name.data ? command_name.data : "",
                     field_name);
    if (n < 0) return ctx_oom(ctx);
    return eval_var_set_current(ctx, nob_sv_from_cstr(buf), value);
}

static bool ctest_record_parsed_field(Arena *arena, Ctest_Parse_Result *out_res, String_View key, String_View value) {
    if (!arena || !out_res || key.count == 0) return false;
    Ctest_Parsed_Field field = {
        .key = key,
        .value = value,
    };
    return arena_arr_push(arena, out_res->fields, field);
}

static String_View ctest_parsed_field_value(const Ctest_Parse_Result *parsed, const char *field_name) {
    if (!parsed || !field_name) return nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(parsed->fields); i++) {
        if (eval_sv_eq_ci_lit(parsed->fields[i].key, field_name)) return parsed->fields[i].value;
    }
    return nob_sv_from_cstr("");
}

static bool ctest_keyword_in_list(String_View tok, const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (eval_sv_eq_ci_lit(tok, items[i])) return true;
    }
    return false;
}

static bool ctest_is_any_known_keyword(String_View tok, const Ctest_Parse_Spec *spec) {
    return ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count) ||
           ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count) ||
           ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count);
}

static bool ctest_publish_var_keyword(Evaluator_Context *ctx, String_View key, String_View value) {
    if (value.count == 0) return true;
    if (eval_sv_eq_ci_lit(key, "RETURN_VALUE") ||
        eval_sv_eq_ci_lit(key, "CAPTURE_CMAKE_ERROR") ||
        eval_sv_eq_ci_lit(key, "NUMBER_ERRORS") ||
        eval_sv_eq_ci_lit(key, "NUMBER_WARNINGS") ||
        eval_sv_eq_ci_lit(key, "DEFECT_COUNT")) {
        return eval_var_set_current(ctx, value, nob_sv_from_cstr("0"));
    }
    return true;
}

static bool ctest_set_session_field(Evaluator_Context *ctx, const char *field_name, String_View value) {
    if (!ctx || !field_name) return false;
    size_t total = sizeof("NOBIFY_CTEST_SESSION::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    int n = snprintf(buf, total + 1, "NOBIFY_CTEST_SESSION::%s", field_name);
    if (n < 0) return ctx_oom(ctx);
    return eval_var_set_current(ctx, nob_sv_from_cstr(buf), value);
}

static String_View ctest_get_session_field(Evaluator_Context *ctx, const char *field_name) {
    if (!ctx || !field_name) return nob_sv_from_cstr("");
    size_t total = sizeof("NOBIFY_CTEST_SESSION::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    int n = snprintf(buf, total + 1, "NOBIFY_CTEST_SESSION::%s", field_name);
    if (n < 0) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return eval_var_get_visible(ctx, nob_sv_from_cstr(buf));
}

static String_View ctest_source_root(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"));
    return v.count > 0 ? v : eval_current_source_dir(ctx);
}

static String_View ctest_resolve_source_dir(Evaluator_Context *ctx, String_View raw) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw.count == 0) return nob_sv_from_cstr("");
    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw, eval_current_source_dir_for_paths(ctx), false);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_normalize_temp(ctx, resolved);
}

static String_View ctest_resolve_binary_dir(Evaluator_Context *ctx, String_View raw) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw.count == 0) return nob_sv_from_cstr("");
    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw, ctest_current_binary_dir(ctx), false);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_normalize_temp(ctx, resolved);
}

static bool ctest_publish_common_dirs(Evaluator_Context *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return false;
    if (source_dir.count > 0 && !eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY"), source_dir)) return false;
    if (binary_dir.count > 0 && !eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY"), binary_dir)) return false;
    return true;
}

static bool ctest_path_exists(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c);
}

static bool ctest_ensure_directory(Evaluator_Context *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return false;
    char *dir_c = eval_sv_to_cstr_temp(ctx, dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, dir_c, false);
    return nob_mkdir_if_not_exists(dir_c);
}

static String_View ctest_session_resolved_build_dir(Evaluator_Context *ctx) {
    String_View build = ctest_get_session_field(ctx, "BUILD");
    if (build.count > 0) return build;
    build = ctest_binary_root(ctx);
    return ctest_resolve_binary_dir(ctx, build);
}

static String_View ctest_session_resolved_source_dir(Evaluator_Context *ctx) {
    String_View source = ctest_get_session_field(ctx, "SOURCE");
    if (source.count > 0) return source;
    source = ctest_source_root(ctx);
    return ctest_resolve_source_dir(ctx, source);
}

static String_View ctest_testing_root(Evaluator_Context *ctx, String_View build_dir) {
    if (!ctx || build_dir.count == 0) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), build_dir, nob_sv_from_cstr("Testing"));
}

static String_View ctest_tag_dir(Evaluator_Context *ctx, String_View testing_dir, String_View tag) {
    if (!ctx || testing_dir.count == 0 || tag.count == 0) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), testing_dir, tag);
}

static bool ctest_read_first_line_temp(Evaluator_Context *ctx, String_View path, String_View *out_line) {
    if (!ctx || !out_line) return false;
    *out_line = nob_sv_from_cstr("");

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return false;

    size_t end = 0;
    while (end < sb.count && sb.items[end] != '\n' && sb.items[end] != '\r') end++;
    *out_line = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, end));
    nob_sb_free(sb);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static String_View ctest_generate_tag_temp(Evaluator_Context *ctx) {
    (void)ctx;
    s_ctest_session_tag_counter++;
    return nob_sv_from_cstr(nob_temp_sprintf("cmk2nob-ctest-%zu", s_ctest_session_tag_counter));
}

static bool ctest_write_manifest(Evaluator_Context *ctx,
                                 String_View command_name,
                                 String_View manifest_path,
                                 String_View tag,
                                 String_View track,
                                 String_View parts,
                                 String_View files) {
    if (!ctx || manifest_path.count == 0) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "COMMAND=");
    nob_sb_append_buf(&sb, command_name.data, command_name.count);
    nob_sb_append_cstr(&sb, "\nTAG=");
    nob_sb_append_buf(&sb, tag.data ? tag.data : "", tag.count);
    nob_sb_append_cstr(&sb, "\nTRACK=");
    nob_sb_append_buf(&sb, track.data ? track.data : "", track.count);
    nob_sb_append_cstr(&sb, "\nPARTS=");
    nob_sb_append_buf(&sb, parts.data ? parts.data : "", parts.count);
    nob_sb_append_cstr(&sb, "\nFILES=");
    nob_sb_append_buf(&sb, files.data ? files.data : "", files.count);
    nob_sb_append_cstr(&sb, "\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, manifest_path, contents, false);
}

static bool ctest_ensure_session_tag(Evaluator_Context *ctx,
                                     String_View model,
                                     bool append_mode,
                                     String_View *out_tag,
                                     String_View *out_testing_dir,
                                     String_View *out_tag_file,
                                     String_View *out_tag_dir) {
    if (!ctx || !out_tag || !out_testing_dir || !out_tag_file || !out_tag_dir) return false;

    String_View build_dir = ctest_session_resolved_build_dir(ctx);
    String_View testing_dir = ctest_get_session_field(ctx, "TESTING_DIR");
    if (testing_dir.count == 0) testing_dir = ctest_testing_root(ctx, build_dir);
    if (eval_should_stop(ctx)) return false;

    String_View tag_file = eval_sv_path_join(eval_temp_arena(ctx), testing_dir, nob_sv_from_cstr("TAG"));
    if (eval_should_stop(ctx)) return false;

    String_View tag = ctest_get_session_field(ctx, "TAG");
    if (tag.count == 0 && append_mode && ctest_path_exists(ctx, tag_file)) {
        if (!ctest_read_first_line_temp(ctx, tag_file, &tag)) return false;
    }
    if (tag.count == 0) tag = ctest_generate_tag_temp(ctx);

    String_View tag_dir_path = ctest_tag_dir(ctx, testing_dir, tag);
    if (eval_should_stop(ctx)) return false;

    if (!ctest_ensure_directory(ctx, testing_dir)) return false;
    if (!ctest_ensure_directory(ctx, tag_dir_path)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, tag.data, tag.count);
    nob_sb_append_cstr(&sb, "\n");
    nob_sb_append_buf(&sb, model.data ? model.data : "", model.count);
    nob_sb_append_cstr(&sb, "\n");
    String_View tag_contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    if (!eval_write_text_file(ctx, tag_file, tag_contents, false)) return false;

    if (!ctest_set_session_field(ctx, "TAG", tag)) return false;
    if (!ctest_set_session_field(ctx, "TESTING_DIR", testing_dir)) return false;
    if (!ctest_set_session_field(ctx, "TAG_FILE", tag_file)) return false;
    if (!ctest_set_session_field(ctx, "TAG_DIR", tag_dir_path)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_TAG"), tag)) return false;
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), build_dir)) return false;

    *out_tag = tag;
    *out_testing_dir = testing_dir;
    *out_tag_file = tag_file;
    *out_tag_dir = tag_dir_path;
    return true;
}

static bool ctest_resolve_files(Evaluator_Context *ctx,
                                const Node *node,
                                String_View command_name,
                                String_View raw_files,
                                String_View base_dir,
                                bool required,
                                bool validate_exists,
                                String_View *out_resolved) {
    if (!ctx || !node || !out_resolved) return false;
    *out_resolved = nob_sv_from_cstr("");

    if (raw_files.count == 0) {
        if (required) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest command requires FILES"),
                                  command_name);
            return false;
        }
        return true;
    }

    SV_List raw_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_files, &raw_items)) return false;

    SV_List resolved_items = {0};
    for (size_t i = 0; i < arena_arr_len(raw_items); i++) {
        if (raw_items[i].count == 0) continue;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw_items[i], base_dir, false);
        resolved = eval_sv_path_normalize_temp(ctx, resolved);
        if (eval_should_stop(ctx)) return false;
        if (validate_exists && !ctest_path_exists(ctx, resolved)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest command file does not exist"),
                                  resolved);
            return false;
        }
        if (!svu_list_push_temp(ctx, &resolved_items, resolved)) return false;
    }

    if (required && arena_arr_len(resolved_items) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest command requires one or more files"),
                              command_name);
        return false;
    }

    *out_resolved = eval_sv_join_semi_temp(ctx, resolved_items, arena_arr_len(resolved_items));
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool ctest_resolve_scripts(Evaluator_Context *ctx,
                                  const SV_List raw_scripts,
                                  String_View base_dir,
                                  String_View **out_resolved_scripts) {
    if (!ctx || !out_resolved_scripts) return false;
    *out_resolved_scripts = NULL;

    for (size_t i = 0; i < arena_arr_len(raw_scripts); i++) {
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw_scripts[i], base_dir, false);
        resolved = eval_sv_path_normalize_temp(ctx, resolved);
        if (eval_should_stop(ctx)) return false;
        if (!eval_sv_arr_push_temp(ctx, out_resolved_scripts, resolved)) return false;
    }
    return true;
}

static bool ctest_new_process_begin(Evaluator_Context *ctx, Ctest_New_Process_State *state) {
    if (!ctx || !state) return false;
    memset(state, 0, sizeof(*state));

    state->message_check_count = arena_arr_len(ctx->message_check_stack);
    state->user_commands_count = arena_arr_len(ctx->command_state.user_commands);
    state->known_targets_count = arena_arr_len(ctx->command_state.known_targets);
    state->alias_targets_count = arena_arr_len(ctx->command_state.alias_targets);
    state->dependency_provider_command_name = ctx->command_state.dependency_provider.command_name;
    state->dependency_provider_supports_find_package = ctx->command_state.dependency_provider.supports_find_package;
    state->dependency_provider_supports_fetchcontent_makeavailable_serial =
        ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial;
    state->dependency_provider_active_find_package_depth =
        ctx->command_state.dependency_provider.active_find_package_depth;
    state->dependency_provider_active_fetchcontent_makeavailable_depth =
        ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth;
    state->active_find_packages_count = arena_arr_len(ctx->command_state.active_find_packages);
    state->fetchcontent_declarations_count = arena_arr_len(ctx->command_state.fetchcontent_declarations);
    state->fetchcontent_states_count = arena_arr_len(ctx->command_state.fetchcontent_states);
    state->active_fetchcontent_makeavailable_count =
        arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable);
    state->watched_variables_count = arena_arr_len(ctx->command_state.watched_variables);
    state->watched_variable_commands_count = arena_arr_len(ctx->command_state.watched_variable_commands);
    state->cpack_component_module_loaded = ctx->cpack_component_module_loaded;
    state->fetchcontent_module_loaded = ctx->fetchcontent_module_loaded;

    if (!eval_scope_push(ctx)) return false;
    state->scope_pushed = true;
    return true;
}

static void ctest_new_process_end(Evaluator_Context *ctx, const Ctest_New_Process_State *state) {
    if (!ctx || !state) return;

    if (state->scope_pushed) eval_scope_pop(ctx);
    if (ctx->message_check_stack) {
        arena_arr_set_len(ctx->message_check_stack, state->message_check_count);
    }
    if (ctx->command_state.user_commands) {
        arena_arr_set_len(ctx->command_state.user_commands, state->user_commands_count);
    }
    if (ctx->command_state.known_targets) {
        arena_arr_set_len(ctx->command_state.known_targets, state->known_targets_count);
    }
    if (ctx->command_state.alias_targets) {
        arena_arr_set_len(ctx->command_state.alias_targets, state->alias_targets_count);
    }
    ctx->command_state.dependency_provider.command_name = state->dependency_provider_command_name;
    ctx->command_state.dependency_provider.supports_find_package = state->dependency_provider_supports_find_package;
    ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial =
        state->dependency_provider_supports_fetchcontent_makeavailable_serial;
    ctx->command_state.dependency_provider.active_find_package_depth =
        state->dependency_provider_active_find_package_depth;
    ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth =
        state->dependency_provider_active_fetchcontent_makeavailable_depth;
    if (ctx->command_state.active_find_packages) {
        arena_arr_set_len(ctx->command_state.active_find_packages, state->active_find_packages_count);
    }
    if (ctx->command_state.fetchcontent_declarations) {
        arena_arr_set_len(ctx->command_state.fetchcontent_declarations, state->fetchcontent_declarations_count);
    }
    if (ctx->command_state.fetchcontent_states) {
        arena_arr_set_len(ctx->command_state.fetchcontent_states, state->fetchcontent_states_count);
    }
    if (ctx->command_state.active_fetchcontent_makeavailable) {
        arena_arr_set_len(ctx->command_state.active_fetchcontent_makeavailable,
                          state->active_fetchcontent_makeavailable_count);
    }
    if (ctx->command_state.watched_variables) {
        arena_arr_set_len(ctx->command_state.watched_variables, state->watched_variables_count);
    }
    if (ctx->command_state.watched_variable_commands) {
        arena_arr_set_len(ctx->command_state.watched_variable_commands, state->watched_variable_commands_count);
    }
    ctx->cpack_component_module_loaded = state->cpack_component_module_loaded;
    ctx->fetchcontent_module_loaded = state->fetchcontent_module_loaded;
    ctx->break_requested = false;
    ctx->continue_requested = false;
    eval_clear_return_state(ctx);
    eval_clear_stop_if_not_oom(ctx);
}

static bool ctest_apply_resolved_context(Evaluator_Context *ctx,
                                         String_View command_name,
                                         const Ctest_Parse_Result *parsed,
                                         bool needs_source,
                                         bool needs_build) {
    if (!ctx || !parsed) return false;

    String_View resolved_source = nob_sv_from_cstr("");
    String_View resolved_build = nob_sv_from_cstr("");

    if (needs_source) {
        String_View source = ctest_parsed_field_value(parsed, "SOURCE");
        if (source.count > 0) {
            resolved_source = ctest_resolve_source_dir(ctx, source);
        } else {
            resolved_source = ctest_session_resolved_source_dir(ctx);
        }
        if (eval_should_stop(ctx)) return false;
        if (!ctest_set_field(ctx, command_name, "RESOLVED_SOURCE", resolved_source)) return false;
    }

    if (needs_build) {
        String_View build = ctest_parsed_field_value(parsed, "BUILD");
        if (build.count > 0) {
            resolved_build = ctest_resolve_binary_dir(ctx, build);
        } else {
            resolved_build = ctest_session_resolved_build_dir(ctx);
        }
        if (eval_should_stop(ctx)) return false;
        if (!ctest_set_field(ctx, command_name, "RESOLVED_BUILD", resolved_build)) return false;
    }

    return ctest_publish_common_dirs(ctx, resolved_source, resolved_build);
}

static bool ctest_parse_generic(Evaluator_Context *ctx,
                                const Node *node,
                                String_View command_name,
                                const SV_List *args,
                                const Ctest_Parse_Spec *spec,
                                Ctest_Parse_Result *out_res) {
    if (!ctx || !node || !args || !spec || !out_res) return false;
    memset(out_res, 0, sizeof(*out_res));

    for (size_t i = 0; i < arena_arr_len(*args); i++) {
        String_View tok = (*args)[i];

        if (ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count)) {
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, nob_sv_from_cstr("1"))) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), nob_sv_from_cstr("1"))) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count)) {
            if (i + 1 >= arena_arr_len(*args)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command keyword requires a value"),
                                      tok);
                return false;
            }
            String_View value = (*args)[++i];
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, value)) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), value)) return false;
            if (!ctest_publish_var_keyword(ctx, tok, value)) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count)) {
            SV_List items = {0};
            size_t j = i + 1;
            for (; j < arena_arr_len(*args); j++) {
                if (ctest_is_any_known_keyword((*args)[j], spec)) break;
                if (!svu_list_push_temp(ctx, &items, (*args)[j])) return false;
            }
            if (arena_arr_len(items) == 0) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command list keyword requires one or more values"),
                                      tok);
                return false;
            }
            String_View joined = eval_sv_join_semi_temp(ctx, items, arena_arr_len(items));
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, joined)) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), joined)) {
                return false;
            }
            i = j - 1;
            continue;
        }

        if (!svu_list_push_temp(ctx, &out_res->positionals, tok)) return false;
    }

    if (arena_arr_len(out_res->positionals) < spec->min_positionals || arena_arr_len(out_res->positionals) > spec->max_positionals) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest command received an invalid number of positional arguments"),
                              command_name);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(out_res->positionals); i++) {
        char field[32];
        snprintf(field, sizeof(field), "POSITIONAL_%zu", i);
        if (!ctest_set_field(ctx, command_name, field, out_res->positionals[i])) return false;
    }

    return true;
}

static String_View ctest_current_binary_dir(Evaluator_Context *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View ctest_binary_root(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static bool ctest_path_is_within(String_View path, String_View root) {
    if (root.count == 0) return false;
    if (path.count < root.count) return false;
    if (memcmp(path.data, root.data, root.count) != 0) return false;
    if (path.count == root.count) return true;
    char next = path.data[root.count];
    return next == '/' || next == '\\';
}

static bool ctest_remove_leaf(const char *path, bool is_dir) {
#if defined(_WIN32)
    if (is_dir) return RemoveDirectoryA(path) != 0;
    return DeleteFileA(path) != 0;
#else
    if (is_dir) return rmdir(path) == 0;
    return unlink(path) == 0;
#endif
}

static bool ctest_remove_tree(const char *path) {
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
            if (!ctest_remove_tree(file.path) || !ctest_remove_leaf(file.path, true)) {
                ok = false;
                break;
            }
        } else if (!ctest_remove_leaf(file.path, false)) {
            ok = false;
            break;
        }
    }
    tinydir_close(&dir);
    return ok;
}

static bool ctest_handle_metadata_with_context(Evaluator_Context *ctx,
                                               const Node *node,
                                               String_View command_name,
                                               const Ctest_Parse_Spec *spec,
                                               bool needs_source,
                                               bool needs_build) {
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));

    Ctest_Parse_Result parsed = {0};
    if (!ctest_parse_generic(ctx, node, command_name, &args, spec, &parsed)) return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    if (!ctest_apply_resolved_context(ctx, command_name, &parsed, needs_source, needs_build)) {
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    return eval_ctest_publish_metadata(ctx, command_name, &args, nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_build(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "CONFIGURATION", "PARALLEL_LEVEL", "FLAGS", "PROJECT_NAME", "TARGET",
        "NUMBER_ERRORS", "NUMBER_WARNINGS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const flags[] = {"APPEND", "QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_build"), &spec, false, true));
}

Eval_Result eval_handle_ctest_configure(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "SOURCE", "OPTIONS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_configure"), &spec, true, true));
}

Eval_Result eval_handle_ctest_coverage(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const multi[] = {"LABELS"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_coverage"), &spec, false, true));
}

Eval_Result eval_handle_ctest_empty_binary_directory(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(args) != 1) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() requires exactly one directory argument"),
                              nob_sv_from_cstr("Usage: ctest_empty_binary_directory(<dir>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View root = eval_sv_path_normalize_temp(ctx, ctest_binary_root(ctx));
    String_View target = eval_path_resolve_for_cmake_arg(ctx, args[0], ctest_current_binary_dir(ctx), false);
    target = eval_sv_path_normalize_temp(ctx, target);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    if (target.count == 0 || root.count == 0 || !ctest_path_is_within(target, root)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() refuses to operate outside CMAKE_BINARY_DIR"),
                              target);
        return eval_result_from_ctx(ctx);
    }

    char *target_c = eval_sv_to_cstr_temp(ctx, target);
    EVAL_OOM_RETURN_IF_NULL(ctx, target_c, eval_result_fatal());

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, target_c) == 0) {
        tinydir_close(&dir);
        if (!ctest_remove_tree(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to remove directory contents"),
                                  target);
            return eval_result_from_ctx(ctx);
        }
        if (!nob_mkdir_if_not_exists(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to recreate directory"),
                                  target);
            return eval_result_from_ctx(ctx);
        }
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_empty_binary_directory"), "DIRECTORY", target)) return eval_result_fatal();
    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_empty_binary_directory"), &args, nob_sv_from_cstr("CLEARED")));
}

Eval_Result eval_handle_ctest_memcheck(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "DEFECT_COUNT", "PARALLEL_LEVEL"
    };
    static const char *const flags[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_memcheck"), &spec, false, true));
}

Eval_Result eval_handle_ctest_read_custom_files(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(args) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_read_custom_files() requires one or more directories"),
                              nob_sv_from_cstr("Usage: ctest_read_custom_files(<dir>...)"));
        return eval_result_from_ctx(ctx);
    }

    SV_List loaded = {0};
    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View dir = eval_path_resolve_for_cmake_arg(ctx, args[i], base_dir, false);
        String_View custom = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr("CTestCustom.cmake"));
        if (eval_should_stop(ctx)) return eval_result_fatal();

        tinydir_file probe = {0};
        char *custom_c = eval_sv_to_cstr_temp(ctx, custom);
        EVAL_OOM_RETURN_IF_NULL(ctx, custom_c, eval_result_fatal());
        if (tinydir_file_open(&probe, custom_c) == 0) {
            Eval_Result exec_res = eval_execute_file(ctx, custom, false, nob_sv_from_cstr(""));
            if (eval_result_is_fatal(exec_res)) return exec_res;
            if (!svu_list_push_temp(ctx, &loaded, custom)) return eval_result_fatal();
        }
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_read_custom_files"),
                         "LOADED_FILES",
                         eval_sv_join_semi_temp(ctx, loaded, arena_arr_len(loaded)))) {
        return eval_result_fatal();
    }
    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_read_custom_files"), &args, nob_sv_from_cstr("MODELED")));
}

static bool ctest_parse_double(String_View sv, double *out_value) {
    if (!out_value) return false;
    if (sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    double v = strtod(buf, &end);
    if (!end || *end != '\0') return false;
    *out_value = v;
    return true;
}

Eval_Result eval_handle_ctest_run_script(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    bool new_process = false;
    String_View return_var = nob_sv_from_cstr("");
    SV_List scripts = {0};

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "NEW_PROCESS")) {
            new_process = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "RETURN_VALUE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest_run_script(RETURN_VALUE ...) requires a variable"),
                                      nob_sv_from_cstr("Usage: ctest_run_script(... RETURN_VALUE <var>)"));
                return eval_result_from_ctx(ctx);
            }
            return_var = args[++i];
            continue;
        }
        if (!svu_list_push_temp(ctx, &scripts, args[i])) return eval_result_fatal();
    }

    if (arena_arr_len(scripts) == 0) {
        String_View current = eval_current_list_file(ctx);
        if (current.count == 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_run_script() needs a script when no current list file is available"),
                                  nob_sv_from_cstr("Usage: ctest_run_script(<script>...)"));
            return eval_result_from_ctx(ctx);
        }
        if (!svu_list_push_temp(ctx, &scripts, current)) return eval_result_fatal();
    }

    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    String_View *resolved_scripts = NULL;
    if (!ctest_resolve_scripts(ctx, scripts, base_dir, &resolved_scripts)) return eval_result_fatal();

    const char *rv_text = "0";
    for (size_t i = 0; i < arena_arr_len(resolved_scripts); i++) {
        size_t error_count_before = ctx->runtime_state.run_report.error_count;
        Eval_Result exec_res = eval_result_fatal();
        if (new_process) {
            Ctest_New_Process_State child_state = {0};
            if (!ctest_new_process_begin(ctx, &child_state)) return eval_result_fatal();
            exec_res = eval_execute_file(ctx, resolved_scripts[i], false, nob_sv_from_cstr(""));
            ctest_new_process_end(ctx, &child_state);
            if (ctx->oom) return eval_result_fatal();
        } else {
            exec_res = eval_execute_file(ctx, resolved_scripts[i], false, nob_sv_from_cstr(""));
        }
        bool script_failed = eval_result_is_fatal(exec_res) ||
                             ctx->runtime_state.run_report.error_count > error_count_before;
        if (script_failed) {
            rv_text = "1";
            break;
        }
    }

    if (return_var.count > 0 && !eval_var_set_current(ctx, return_var, nob_sv_from_cstr(rv_text))) return eval_result_fatal();
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "EXECUTION_MODE",
                         new_process ? nob_sv_from_cstr("NEW_PROCESS") : nob_sv_from_cstr("IN_PROCESS"))) {
        return eval_result_fatal();
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "SCRIPTS",
                         eval_sv_join_semi_temp(ctx, scripts, arena_arr_len(scripts)))) {
        return eval_result_fatal();
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "RESOLVED_SCRIPTS",
                         eval_sv_join_semi_temp(ctx, resolved_scripts, arena_arr_len(resolved_scripts)))) {
        return eval_result_fatal();
    }
    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_run_script"), &args, nob_sv_from_cstr("MODELED")));
}

Eval_Result eval_handle_ctest_sleep(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (!(arena_arr_len(args) == 1 || arena_arr_len(args) == 3)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_sleep() expects one or three numeric arguments"),
                              nob_sv_from_cstr("Usage: ctest_sleep(<seconds>) or ctest_sleep(<time1> <duration> <time2>)"));
        return eval_result_from_ctx(ctx);
    }

    double values[3] = {0};
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (!ctest_parse_double(args[i], &values[i])) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_sleep() requires numeric arguments"),
                                  args[i]);
            return eval_result_from_ctx(ctx);
        }
    }

    String_View duration = nob_sv_from_cstr("0");
    if (arena_arr_len(args) == 1) {
        duration = args[0];
    } else {
        double computed = values[0] + values[1] - values[2];
        if (computed < 0.0) computed = 0.0;
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%.3f", computed);
        if (n < 0 || (size_t)n >= sizeof(buf)) {
            (void)ctx_oom(ctx);
            return eval_result_fatal();
        }
        duration = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_sleep"), "DURATION", duration)) return eval_result_fatal();
    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_sleep"), &args, nob_sv_from_cstr("NOOP")));
}

Eval_Result eval_handle_ctest_start(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"TRACK"};
    static const char *const flags[] = {"APPEND"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 1, 3};
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Ctest_Parse_Result parsed = {0};
    if (!ctest_parse_generic(ctx, node, nob_sv_from_cstr("ctest_start"), &args, &spec, &parsed)) {
        return eval_result_from_ctx(ctx);
    }

    String_View model = parsed.positionals[0];
    String_View source = arena_arr_len(parsed.positionals) >= 2 ? parsed.positionals[1] : ctest_source_root(ctx);
    String_View build = arena_arr_len(parsed.positionals) >= 3 ? parsed.positionals[2] : ctest_binary_root(ctx);
    String_View track = ctest_parsed_field_value(&parsed, "TRACK");
    bool append_mode = ctest_parsed_field_value(&parsed, "APPEND").count > 0;

    String_View resolved_source = ctest_resolve_source_dir(ctx, source);
    String_View resolved_build = ctest_resolve_binary_dir(ctx, build);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "MODEL", model)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "RESOLVED_SOURCE", resolved_source)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "RESOLVED_BUILD", resolved_build)) return eval_result_fatal();

    if (!ctest_set_session_field(ctx, "MODEL", model)) return eval_result_fatal();
    if (!ctest_set_session_field(ctx, "SOURCE", resolved_source)) return eval_result_fatal();
    if (!ctest_set_session_field(ctx, "BUILD", resolved_build)) return eval_result_fatal();
    if (!ctest_set_session_field(ctx, "TRACK", track)) return eval_result_fatal();

    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx, model, append_mode, &tag, &testing_dir, &tag_file, &tag_dir_path)) {
        return eval_result_fatal();
    }

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_MODEL"), model)) return eval_result_fatal();
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_TRACK"), track)) return eval_result_fatal();
    if (!ctest_publish_common_dirs(ctx, resolved_source, resolved_build)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG", tag)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TESTING_DIR", testing_dir)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG_FILE", tag_file)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG_DIR", tag_dir_path)) return eval_result_fatal();

    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_start"), &args, nob_sv_from_cstr("STAGED")));
}

Eval_Result eval_handle_ctest_submit(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"RETRY_COUNT", "RETRY_DELAY", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"};
    static const char *const multi[] = {"PARTS", "FILES"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Ctest_Parse_Result parsed = {0};
    if (!ctest_parse_generic(ctx, node, nob_sv_from_cstr("ctest_submit"), &args, &spec, &parsed)) {
        return eval_result_from_ctx(ctx);
    }

    String_View model = ctest_get_session_field(ctx, "MODEL");
    if (model.count == 0) model = nob_sv_from_cstr("Experimental");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx, model, false, &tag, &testing_dir, &tag_file, &tag_dir_path)) {
        return eval_result_fatal();
    }

    String_View build_dir = ctest_session_resolved_build_dir(ctx);
    String_View parts = ctest_parsed_field_value(&parsed, "PARTS");
    String_View files = nob_sv_from_cstr("");
    if (!ctest_resolve_files(ctx,
                             node,
                             nob_sv_from_cstr("ctest_submit"),
                             ctest_parsed_field_value(&parsed, "FILES"),
                             build_dir.count > 0 ? build_dir : ctest_current_binary_dir(ctx),
                             false,
                             true,
                             &files)) {
        return eval_result_from_ctx(ctx);
    }

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("SubmitManifest.txt"));
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_submit"),
                              manifest,
                              tag,
                              ctest_get_session_field(ctx, "TRACK"),
                              parts,
                              files)) {
        return eval_result_fatal();
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TAG", tag)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TAG_FILE", tag_file)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TESTING_DIR", testing_dir)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "MANIFEST", manifest)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESOLVED_PARTS", parts)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESOLVED_FILES", files)) return eval_result_fatal();

    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_submit"), &args, nob_sv_from_cstr("STAGED")));
}

Eval_Result eval_handle_ctest_test(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "PARALLEL_LEVEL",
        "STOP_TIME", "TEST_LOAD", "OUTPUT_JUNIT"
    };
    static const char *const flags[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_test"), &spec, false, true));
}

Eval_Result eval_handle_ctest_update(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"SOURCE", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return eval_result_from_bool(ctest_handle_metadata_with_context(ctx, node, nob_sv_from_cstr("ctest_update"), &spec, true, false));
}

Eval_Result eval_handle_ctest_upload(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"CAPTURE_CMAKE_ERROR"};
    static const char *const multi[] = {"FILES"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), NULL, 0, 0, 0};
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Ctest_Parse_Result parsed = {0};
    if (!ctest_parse_generic(ctx, node, nob_sv_from_cstr("ctest_upload"), &args, &spec, &parsed)) {
        return eval_result_from_ctx(ctx);
    }

    String_View model = ctest_get_session_field(ctx, "MODEL");
    if (model.count == 0) model = nob_sv_from_cstr("Experimental");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx, model, false, &tag, &testing_dir, &tag_file, &tag_dir_path)) {
        return eval_result_fatal();
    }

    String_View build_dir = ctest_session_resolved_build_dir(ctx);
    String_View files = nob_sv_from_cstr("");
    if (!ctest_resolve_files(ctx,
                             node,
                             nob_sv_from_cstr("ctest_upload"),
                             ctest_parsed_field_value(&parsed, "FILES"),
                             build_dir.count > 0 ? build_dir : ctest_current_binary_dir(ctx),
                             true,
                             true,
                             &files)) {
        return eval_result_from_ctx(ctx);
    }

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("UploadManifest.txt"));
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_upload"),
                              manifest,
                              tag,
                              ctest_get_session_field(ctx, "TRACK"),
                              nob_sv_from_cstr(""),
                              files)) {
        return eval_result_fatal();
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TAG", tag)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TAG_FILE", tag_file)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TESTING_DIR", testing_dir)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "MANIFEST", manifest)) return eval_result_fatal();
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "RESOLVED_FILES", files)) return eval_result_fatal();

    return eval_result_from_bool(eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_upload"), &args, nob_sv_from_cstr("STAGED")));
}
