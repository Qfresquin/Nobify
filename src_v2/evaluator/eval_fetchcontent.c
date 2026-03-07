#include "eval_fetchcontent.h"

#include "eval_flow_internal.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    String_View source_subdir;
    bool has_source_dir;
    bool has_binary_dir;
    bool exclude_from_all;
    bool system;
} FetchContent_Declaration_Info;

typedef struct {
    String_View source_dir_var;
    String_View binary_dir_var;
    String_View populated_var;
} FetchContent_GetProperties_Options;

static bool fetchcontent_require_module(Evaluator_Context *ctx,
                                        String_View command,
                                        Cmake_Event_Origin origin) {
    if (!ctx) return false;
    if (ctx->fetchcontent_module_loaded) return true;
    EVAL_DIAG_EMIT_SEV(ctx,
                       EV_DIAG_ERROR,
                       EVAL_DIAG_UNKNOWN_COMMAND,
                       nob_sv_from_cstr("dispatcher"),
                       command,
                       origin,
                       nob_sv_from_cstr("Unknown command"),
                       nob_sv_from_cstr("include(FetchContent) must be called before using this command"));
    return false;
}

static String_View fetchcontent_ascii_case_copy(Arena *arena, String_View in, int (*map_fn)(int)) {
    if (!arena || !in.data) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = (char)map_fn((unsigned char)in.data[i]);
    }
    buf[in.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View fetchcontent_lower_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(eval_temp_arena(ctx), in, tolower);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static String_View fetchcontent_lower_event(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(ctx->event_arena, in, tolower);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static String_View fetchcontent_upper_temp(Evaluator_Context *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(eval_temp_arena(ctx), in, toupper);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static Eval_FetchContent_Declaration *fetchcontent_find_declaration(Evaluator_Context *ctx,
                                                                    String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->command_state.fetchcontent_declarations); i++) {
        Eval_FetchContent_Declaration *decl = &ctx->command_state.fetchcontent_declarations[i];
        if (eval_sv_key_eq(decl->canonical_name, canonical_name)) return decl;
    }
    return NULL;
}

static Eval_FetchContent_State *fetchcontent_find_state(Evaluator_Context *ctx,
                                                        String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->command_state.fetchcontent_states); i++) {
        Eval_FetchContent_State *state = &ctx->command_state.fetchcontent_states[i];
        if (eval_sv_key_eq(state->canonical_name, canonical_name)) return state;
    }
    return NULL;
}

static bool fetchcontent_is_declaration_single_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "SOURCE_DIR") ||
           eval_sv_eq_ci_lit(token, "BINARY_DIR") ||
           eval_sv_eq_ci_lit(token, "SOURCE_SUBDIR");
}

static String_View fetchcontent_current_binary_dir(Evaluator_Context *ctx) {
    String_View current = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (current.count == 0) current = ctx ? ctx->binary_dir : nob_sv_from_cstr("");
    return current;
}

static String_View fetchcontent_current_source_dir(Evaluator_Context *ctx) {
    String_View current = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current.count == 0) current = ctx ? ctx->source_dir : nob_sv_from_cstr("");
    return current;
}

static String_View fetchcontent_default_source_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View base = fetchcontent_current_binary_dir(ctx);
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-src");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), base, suffix);
}

static String_View fetchcontent_default_binary_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View base = fetchcontent_current_binary_dir(ctx);
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-build");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), base, suffix);
}

static bool fetchcontent_file_exists(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_file_exists(path_c) != 0;
}

static bool fetchcontent_push_active(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    String_View stable = sv_copy_to_event_arena(ctx, canonical_name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->command_state.active_fetchcontent_makeavailable, stable);
}

static void fetchcontent_pop_active(Evaluator_Context *ctx, bool pushed) {
    if (!ctx || !pushed) return;
    if (arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable) == 0) return;
    arena_arr_set_len(ctx->command_state.active_fetchcontent_makeavailable,
                      arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable) - 1);
}

static bool fetchcontent_active_contains(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable); i++) {
        if (eval_sv_key_eq(ctx->command_state.active_fetchcontent_makeavailable[i], canonical_name)) return true;
    }
    return false;
}

static bool fetchcontent_publish_default_vars(Evaluator_Context *ctx,
                                              String_View dependency_name,
                                              bool populated,
                                              String_View source_dir,
                                              String_View binary_dir) {
    if (!ctx || dependency_name.count == 0) return false;
    String_View lower = fetchcontent_lower_temp(ctx, dependency_name);
    if (eval_should_stop(ctx)) return false;

    String_View source_var = svu_concat_suffix_temp(ctx, lower, "_SOURCE_DIR");
    String_View binary_var = svu_concat_suffix_temp(ctx, lower, "_BINARY_DIR");
    String_View populated_var = svu_concat_suffix_temp(ctx, lower, "_POPULATED");
    if (eval_should_stop(ctx)) return false;

    if (!eval_var_set_current(ctx, source_var, source_dir)) return false;
    if (!eval_var_set_current(ctx, binary_var, binary_dir)) return false;
    if (!eval_var_set_current(ctx, populated_var, populated ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) return false;
    return true;
}

static bool fetchcontent_parse_declaration_info(Evaluator_Context *ctx,
                                                String_View dependency_name,
                                                const SV_List args,
                                                FetchContent_Declaration_Info *out_info) {
    if (!ctx || !out_info) return false;
    memset(out_info, 0, sizeof(*out_info));
    out_info->source_dir = nob_sv_from_cstr("");
    out_info->binary_dir = nob_sv_from_cstr("");
    out_info->source_subdir = nob_sv_from_cstr("");

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View token = args[i];
        if (eval_sv_eq_ci_lit(token, "SOURCE_DIR")) {
            if (i + 1 < arena_arr_len(args)) {
                out_info->source_dir = args[++i];
                out_info->has_source_dir = true;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(token, "BINARY_DIR")) {
            if (i + 1 < arena_arr_len(args)) {
                out_info->binary_dir = args[++i];
                out_info->has_binary_dir = true;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(token, "SOURCE_SUBDIR")) {
            if (i + 1 < arena_arr_len(args)) out_info->source_subdir = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(token, "EXCLUDE_FROM_ALL")) {
            out_info->exclude_from_all = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(token, "SYSTEM")) {
            out_info->system = true;
            continue;
        }
    }

    if (out_info->binary_dir.count == 0) {
        out_info->binary_dir = fetchcontent_default_binary_dir(ctx, fetchcontent_lower_temp(ctx, dependency_name));
        if (eval_should_stop(ctx)) return false;
    }
    return true;
}

static bool fetchcontent_upsert_state(Evaluator_Context *ctx,
                                      String_View dependency_name,
                                      String_View canonical_name,
                                      bool populated,
                                      String_View source_dir,
                                      String_View binary_dir) {
    if (!ctx || dependency_name.count == 0 || canonical_name.count == 0) return false;
    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, canonical_name);
    if (state) {
        state->name = sv_copy_to_event_arena(ctx, dependency_name);
        state->populated = populated;
        state->source_dir = sv_copy_to_event_arena(ctx, source_dir);
        state->binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    Eval_FetchContent_State entry = {0};
    entry.name = sv_copy_to_event_arena(ctx, dependency_name);
    entry.canonical_name = sv_copy_to_event_arena(ctx, canonical_name);
    entry.populated = populated;
    entry.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    entry.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->command_state.fetchcontent_states, entry);
}

static bool fetchcontent_build_provider_args(Evaluator_Context *ctx,
                                            const Eval_FetchContent_Declaration *decl,
                                            SV_List *out_args) {
    if (!ctx || !decl || !out_args) return false;
    *out_args = NULL;

    bool saw_source_dir = false;
    bool saw_binary_dir = false;
    if (!eval_sv_arr_push_temp(ctx, out_args, decl->name)) return false;
    for (size_t i = 0; i < arena_arr_len(decl->args); i++) {
        String_View token = decl->args[i];
        if (eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE")) continue;
        if (eval_sv_eq_ci_lit(token, "SOURCE_DIR")) saw_source_dir = true;
        if (eval_sv_eq_ci_lit(token, "BINARY_DIR")) saw_binary_dir = true;
        if (!eval_sv_arr_push_temp(ctx, out_args, token)) return false;
    }

    if (!saw_source_dir) {
        if (!eval_sv_arr_push_temp(ctx, out_args, nob_sv_from_cstr("SOURCE_DIR"))) return false;
        if (!eval_sv_arr_push_temp(ctx, out_args, fetchcontent_default_source_dir(ctx, decl->canonical_name))) return false;
    }
    if (!saw_binary_dir) {
        if (!eval_sv_arr_push_temp(ctx, out_args, nob_sv_from_cstr("BINARY_DIR"))) return false;
        if (!eval_sv_arr_push_temp(ctx, out_args, fetchcontent_default_binary_dir(ctx, decl->canonical_name))) return false;
    }
    return true;
}

static bool fetchcontent_run_add_subdirectory(Evaluator_Context *ctx,
                                              String_View source_dir,
                                              String_View binary_dir,
                                              bool exclude_from_all,
                                              bool system) {
    if (!ctx) return false;
    SV_List call_args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &call_args, source_dir)) return false;
    if (!eval_sv_arr_push_temp(ctx, &call_args, binary_dir)) return false;
    if (exclude_from_all && !eval_sv_arr_push_temp(ctx, &call_args, nob_sv_from_cstr("EXCLUDE_FROM_ALL"))) return false;
    if (system && !eval_sv_arr_push_temp(ctx, &call_args, nob_sv_from_cstr("SYSTEM"))) return false;

    String_View script = nob_sv_from_cstr("");
    Ast_Root ast = NULL;
    if (!flow_build_call_script(ctx, nob_sv_from_cstr("add_subdirectory"), &call_args, &script)) return false;
    if (!flow_parse_inline_script(ctx, script, &ast)) return false;
    return !eval_result_is_fatal(eval_run_ast_inline(ctx, ast));
}

static bool fetchcontent_makeavailable_builtin(Evaluator_Context *ctx,
                                               const Node *node,
                                               String_View dependency_name,
                                               const Eval_FetchContent_Declaration *decl) {
    if (!ctx || !node || !decl) return false;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    String_View upper = fetchcontent_upper_temp(ctx, dependency_name);
    String_View override_key_parts[2] = {
        nob_sv_from_cstr("FETCHCONTENT_SOURCE_DIR_"),
        upper,
    };
    String_View override_key = svu_join_no_sep_temp(ctx, override_key_parts, 2);
    if (eval_should_stop(ctx)) return false;

    FetchContent_Declaration_Info info = {0};
    if (!fetchcontent_parse_declaration_info(ctx, dependency_name, decl->args, &info)) return false;

    String_View source_dir = eval_var_get_visible(ctx, override_key);
    if (source_dir.count > 0) {
        source_dir = eval_path_resolve_for_cmake_arg(ctx, source_dir, fetchcontent_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return false;
    } else if (info.has_source_dir) {
        source_dir = info.source_dir;
    }

    String_View binary_dir = info.binary_dir;
    if (binary_dir.count == 0) {
        binary_dir = fetchcontent_default_binary_dir(ctx, decl->canonical_name);
        if (eval_should_stop(ctx)) return false;
    }

    if (source_dir.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_NOT_IMPLEMENTED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_MakeAvailable() fallback currently requires SOURCE_DIR or provider fulfillment"),
                                       dependency_name);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    if (!fetchcontent_upsert_state(ctx, dependency_name, decl->canonical_name, true, source_dir, binary_dir)) return false;
    if (!fetchcontent_publish_default_vars(ctx, dependency_name, true, source_dir, binary_dir)) return false;

    String_View add_subdirectory_source = source_dir;
    if (info.source_subdir.count > 0) {
        add_subdirectory_source = eval_sv_path_join(eval_temp_arena(ctx), source_dir, info.source_subdir);
    }
    if (eval_should_stop(ctx)) return false;

    String_View cmakelists = eval_sv_path_join(eval_temp_arena(ctx), add_subdirectory_source, nob_sv_from_cstr("CMakeLists.txt"));
    if (eval_should_stop(ctx)) return false;
    if (fetchcontent_file_exists(ctx, cmakelists)) {
        if (!fetchcontent_run_add_subdirectory(ctx,
                                               add_subdirectory_source,
                                               binary_dir,
                                               info.exclude_from_all,
                                               info.system)) {
            return false;
        }
    }

    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool fetchcontent_invoke_dependency_provider(Evaluator_Context *ctx,
                                                    const Node *node,
                                                    const Eval_FetchContent_Declaration *decl,
                                                    bool *out_populated) {
    if (!ctx || !node || !decl || !out_populated) return false;
    *out_populated = false;

    String_View provider_command_name = ctx->command_state.dependency_provider.command_name;
    if (provider_command_name.count == 0 ||
        !ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial) {
        return true;
    }

    SV_List provider_method_args = NULL;
    SV_List provider_args = NULL;
    if (!fetchcontent_build_provider_args(ctx, decl, &provider_method_args)) return false;
    if (!eval_sv_arr_push_temp(ctx, &provider_args, nob_sv_from_cstr("FETCHCONTENT_MAKEAVAILABLE_SERIAL"))) return false;
    for (size_t i = 0; i < arena_arr_len(provider_method_args); i++) {
        if (!eval_sv_arr_push_temp(ctx, &provider_args, provider_method_args[i])) return false;
    }

    ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth++;
    Eval_Result invoke_result = eval_result_ok();
    if (!eval_user_cmd_invoke(ctx, provider_command_name, &provider_args, eval_origin_from_node(ctx, node))) {
        invoke_result = eval_result_from_ctx(ctx);
    } else {
        invoke_result = eval_result_ok_if_running(ctx);
    }
    if (ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth > 0) {
        ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth--;
    }
    if (eval_result_is_fatal(invoke_result)) return false;

    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, decl->canonical_name);
    if (state && state->populated) *out_populated = true;
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool fetchcontent_makeavailable_one(Evaluator_Context *ctx,
                                           const Node *node,
                                           String_View dependency_name) {
    if (!ctx || !node || dependency_name.count == 0) return false;
    String_View canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    String_View upper_name = fetchcontent_upper_temp(ctx, dependency_name);
    String_View override_key_parts[2] = {
        nob_sv_from_cstr("FETCHCONTENT_SOURCE_DIR_"),
        upper_name,
    };
    String_View override_key = svu_join_no_sep_temp(ctx, override_key_parts, 2);
    bool has_source_override = false;
    if (eval_should_stop(ctx)) return false;
    has_source_override = eval_var_get_visible(ctx, override_key).count > 0;

    bool already_active = fetchcontent_active_contains(ctx, canonical_name);
    bool pushed_active = false;
    if (!already_active) {
        if (!fetchcontent_push_active(ctx, canonical_name)) return false;
        pushed_active = true;
    }

    Eval_FetchContent_State *existing = fetchcontent_find_state(ctx, canonical_name);
    if (existing && existing->populated) {
        bool ok = fetchcontent_publish_default_vars(ctx,
                                                    existing->name.count > 0 ? existing->name : dependency_name,
                                                    true,
                                                    existing->source_dir,
                                                    existing->binary_dir);
        fetchcontent_pop_active(ctx, pushed_active);
        return ok;
    }

    Eval_FetchContent_Declaration *decl = fetchcontent_find_declaration(ctx, canonical_name);
    if (!decl) {
        Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_MakeAvailable() requires a prior FetchContent_Declare()"),
                                       dependency_name);
        fetchcontent_pop_active(ctx, pushed_active);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    bool provider_populated = false;
    if (!already_active &&
        !has_source_override &&
        ctx->command_state.dependency_provider.command_name.count > 0 &&
        ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial) {
        if (!fetchcontent_invoke_dependency_provider(ctx, node, decl, &provider_populated)) {
            fetchcontent_pop_active(ctx, pushed_active);
            return false;
        }
    }

    if (!provider_populated) {
        if (!fetchcontent_makeavailable_builtin(ctx, node, dependency_name, decl)) {
            fetchcontent_pop_active(ctx, pushed_active);
            return false;
        }
    } else {
        Eval_FetchContent_State *state = fetchcontent_find_state(ctx, canonical_name);
        if (state && !fetchcontent_publish_default_vars(ctx, dependency_name, true, state->source_dir, state->binary_dir)) {
            fetchcontent_pop_active(ctx, pushed_active);
            return false;
        }
    }

    fetchcontent_pop_active(ctx, pushed_active);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

Eval_Result eval_handle_fetchcontent_declare(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_Declare() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_Declare(<name> <content options>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View dependency_name = a[0];
    String_View canonical_name = fetchcontent_lower_event(ctx, dependency_name);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (fetchcontent_find_declaration(ctx, canonical_name) != NULL) return eval_result_from_ctx(ctx);

    SV_List stored_args = NULL;
    String_View current_source_dir = fetchcontent_current_source_dir(ctx);
    String_View current_binary_dir = fetchcontent_current_binary_dir(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View token = sv_copy_to_event_arena(ctx, a[i]);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, stored_args, token)) return eval_result_fatal();
        if (!fetchcontent_is_declaration_single_value_key(a[i])) continue;
        if (i + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           origin,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_MISSING_REQUIRED,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Declare() option requires a value"),
                                           a[i]);
            return eval_result_from_ctx(ctx);
        }

        String_View value = a[++i];
        if (eval_sv_eq_ci_lit(token, "SOURCE_DIR")) {
            value = eval_path_resolve_for_cmake_arg(ctx, value, current_source_dir, false);
        } else if (eval_sv_eq_ci_lit(token, "BINARY_DIR")) {
            value = eval_path_resolve_for_cmake_arg(ctx, value, current_binary_dir, false);
        }
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        value = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, stored_args, value)) return eval_result_fatal();
    }

    Eval_FetchContent_Declaration decl = {0};
    decl.name = sv_copy_to_event_arena(ctx, dependency_name);
    decl.canonical_name = canonical_name;
    decl.args = stored_args;
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->command_state.fetchcontent_declarations, decl)) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_makeavailable(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_MakeAvailable() requires at least one dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_MakeAvailable(<name>...)"));
        return eval_result_from_ctx(ctx);
    }

    for (size_t i = 0; i < arena_arr_len(a); i++) {
        if (!fetchcontent_makeavailable_one(ctx, node, a[i])) return eval_result_from_ctx(ctx);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_getproperties(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_GetProperties() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> [SOURCE_DIR <out>] [BINARY_DIR <out>] [POPULATED <out>])"));
        return eval_result_from_ctx(ctx);
    }

    String_View dependency_name = a[0];
    String_View canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    FetchContent_GetProperties_Options opt = {0};
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "SOURCE_DIR")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(SOURCE_DIR) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> SOURCE_DIR <out>)"));
                return eval_result_from_ctx(ctx);
            }
            opt.source_dir_var = a[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BINARY_DIR")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(BINARY_DIR) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> BINARY_DIR <out>)"));
                return eval_result_from_ctx(ctx);
            }
            opt.binary_dir_var = a[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "POPULATED")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(POPULATED) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> POPULATED <out>)"));
                return eval_result_from_ctx(ctx);
            }
            opt.populated_var = a[++i];
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_GetProperties() received an unexpected argument"),
                                       a[i]);
        return eval_result_from_ctx(ctx);
    }

    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, canonical_name);
    String_View source_dir = state ? state->source_dir : nob_sv_from_cstr("");
    String_View binary_dir = state ? state->binary_dir : nob_sv_from_cstr("");
    bool populated = state ? state->populated : false;

    if (opt.source_dir_var.count == 0 && opt.binary_dir_var.count == 0 && opt.populated_var.count == 0) {
        if (!fetchcontent_publish_default_vars(ctx, dependency_name, populated, source_dir, binary_dir)) return eval_result_fatal();
        return eval_result_from_ctx(ctx);
    }

    if (opt.source_dir_var.count > 0 && !eval_var_set_current(ctx, opt.source_dir_var, source_dir)) return eval_result_fatal();
    if (opt.binary_dir_var.count > 0 && !eval_var_set_current(ctx, opt.binary_dir_var, binary_dir)) return eval_result_fatal();
    if (opt.populated_var.count > 0 &&
        !eval_var_set_current(ctx, opt.populated_var, populated ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_setpopulated(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> [SOURCE_DIR <dir>] [BINARY_DIR <dir>])"));
        return eval_result_from_ctx(ctx);
    }
    if (ctx->command_state.dependency_provider.active_fetchcontent_makeavailable_depth == 0 ||
        arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_INVALID_CONTEXT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() may only be used from inside a dependency provider"),
                                       nob_sv_from_cstr("Call it only while handling FETCHCONTENT_MAKEAVAILABLE_SERIAL"));
        return eval_result_from_ctx(ctx);
    }

    String_View dependency_name = a[0];
    String_View canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    String_View active_name =
        ctx->command_state.active_fetchcontent_makeavailable[arena_arr_len(ctx->command_state.active_fetchcontent_makeavailable) - 1];
    if (!eval_sv_key_eq(active_name, canonical_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_INVALID_CONTEXT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() must match the active dependency provider request"),
                                       dependency_name);
        return eval_result_from_ctx(ctx);
    }

    String_View source_dir = nob_sv_from_cstr("");
    String_View binary_dir = nob_sv_from_cstr("");
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "SOURCE_DIR")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_SetPopulated(SOURCE_DIR) requires a directory value"), nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> SOURCE_DIR <dir>)"));
                return eval_result_from_ctx(ctx);
            }
            source_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], fetchcontent_current_source_dir(ctx), false);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BINARY_DIR")) {
            if (i + 1 >= arena_arr_len(a)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_SetPopulated(BINARY_DIR) requires a directory value"), nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> BINARY_DIR <dir>)"));
                return eval_result_from_ctx(ctx);
            }
            binary_dir = eval_path_resolve_for_cmake_arg(ctx, a[++i], fetchcontent_current_binary_dir(ctx), false);
            if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() received an unexpected argument"),
                                       a[i]);
        return eval_result_from_ctx(ctx);
    }

    Eval_FetchContent_Declaration *decl = fetchcontent_find_declaration(ctx, canonical_name);
    if (decl) {
        FetchContent_Declaration_Info info = {0};
        if (!fetchcontent_parse_declaration_info(ctx, dependency_name, decl->args, &info)) return eval_result_from_ctx(ctx);
        if (source_dir.count == 0) source_dir = info.source_dir;
        if (binary_dir.count == 0) binary_dir = info.binary_dir;
    } else {
        if (source_dir.count == 0) source_dir = fetchcontent_default_source_dir(ctx, canonical_name);
        if (binary_dir.count == 0) binary_dir = fetchcontent_default_binary_dir(ctx, canonical_name);
    }
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (!fetchcontent_upsert_state(ctx, dependency_name, canonical_name, true, source_dir, binary_dir)) return eval_result_fatal();
    if (!fetchcontent_publish_default_vars(ctx, dependency_name, true, source_dir, binary_dir)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
