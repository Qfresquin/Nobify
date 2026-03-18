#include "eval_fetchcontent.h"

#include "eval_flow_internal.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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
    for (size_t i = 0; i < in.count; i++) buf[i] = (char)map_fn((unsigned char)in.data[i]);
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
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.declarations); i++) {
        Eval_FetchContent_Declaration *decl = &ctx->semantic_state.fetchcontent.declarations[i];
        if (eval_sv_key_eq(decl->canonical_name, canonical_name)) return decl;
    }
    return NULL;
}

static Eval_FetchContent_State *fetchcontent_find_state(Evaluator_Context *ctx,
                                                        String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.states); i++) {
        Eval_FetchContent_State *state = &ctx->semantic_state.fetchcontent.states[i];
        if (eval_sv_key_eq(state->canonical_name, canonical_name)) return state;
    }
    return NULL;
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

static String_View fetchcontent_saved_base_dir(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View base = eval_var_get_visible(ctx, nob_sv_from_cstr("FETCHCONTENT_BASE_DIR"));
    if (base.count > 0) {
        base = eval_path_resolve_for_cmake_arg(ctx, base, fetchcontent_current_binary_dir(ctx), false);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        return base;
    }

    String_View cmake_binary_dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    if (cmake_binary_dir.count == 0) cmake_binary_dir = fetchcontent_current_binary_dir(ctx);
    return eval_sv_path_join(eval_temp_arena(ctx), cmake_binary_dir, nob_sv_from_cstr("_deps"));
}

static String_View fetchcontent_saved_default_source_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-src");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_saved_base_dir(ctx), suffix);
}

static String_View fetchcontent_saved_default_binary_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-build");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_saved_base_dir(ctx), suffix);
}

static String_View fetchcontent_direct_default_source_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-src");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_current_binary_dir(ctx), suffix);
}

static String_View fetchcontent_direct_default_binary_dir(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-build");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_current_binary_dir(ctx), suffix);
}

static bool fetchcontent_file_exists(Evaluator_Context *ctx, String_View path) {
    bool exists = false;
    return eval_service_file_exists(ctx, path, &exists) && exists;
}

static bool fetchcontent_mkdir_p(Evaluator_Context *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return false;
    String_View marker = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr(".keep"));
    if (eval_should_stop(ctx)) return false;
    if (!eval_mkdirs_for_parent(ctx, marker)) return false;
    return eval_service_mkdir(ctx, dir);
}

static bool fetchcontent_push_active(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    String_View stable = sv_copy_to_event_arena(ctx, canonical_name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.fetchcontent.active_makeavailable, stable);
}

static void fetchcontent_pop_active(Evaluator_Context *ctx, bool pushed) {
    if (!ctx || !pushed) return;
    if (arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) == 0) return;
    arena_arr_set_len(ctx->semantic_state.fetchcontent.active_makeavailable,
                      arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) - 1);
}

static bool fetchcontent_active_contains(Evaluator_Context *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable); i++) {
        if (eval_sv_key_eq(ctx->semantic_state.fetchcontent.active_makeavailable[i], canonical_name)) return true;
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
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.fetchcontent.states, entry);
}

static bool fetchcontent_looks_like_url(String_View value) {
    if (value.count == 0) return false;
    for (size_t i = 0; i + 2 < value.count; i++) {
        if (value.data[i] == ':' && value.data[i + 1] == '/' && value.data[i + 2] == '/') return true;
    }
    return false;
}

static String_View fetchcontent_resolve_local_like_path(Evaluator_Context *ctx,
                                                        String_View raw_value,
                                                        String_View base_dir) {
    if (!ctx || raw_value.count == 0) return nob_sv_from_cstr("");
    if (fetchcontent_looks_like_url(raw_value)) return raw_value;
    return eval_path_resolve_for_cmake_arg(ctx, raw_value, base_dir, false);
}

static bool fetchcontent_is_bool_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "DOWNLOAD_NO_EXTRACT") ||
           eval_sv_eq_ci_lit(token, "DOWNLOAD_EXTRACT_TIMESTAMP") ||
           eval_sv_eq_ci_lit(token, "GIT_SHALLOW") ||
           eval_sv_eq_ci_lit(token, "GIT_PROGRESS") ||
           eval_sv_eq_ci_lit(token, "GIT_SUBMODULES_RECURSE");
}

static bool fetchcontent_is_single_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "SOURCE_DIR") ||
           eval_sv_eq_ci_lit(token, "BINARY_DIR") ||
           eval_sv_eq_ci_lit(token, "SOURCE_SUBDIR") ||
           eval_sv_eq_ci_lit(token, "URL") ||
           eval_sv_eq_ci_lit(token, "URL_HASH") ||
           eval_sv_eq_ci_lit(token, "URL_MD5") ||
           eval_sv_eq_ci_lit(token, "GIT_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "GIT_TAG") ||
           eval_sv_eq_ci_lit(token, "SUBBUILD_DIR");
}

static bool fetchcontent_is_flag_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(token, "SYSTEM") ||
           eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE") ||
           eval_sv_eq_ci_lit(token, "QUIET");
}

static bool fetchcontent_is_multi_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "FIND_PACKAGE_ARGS") ||
           eval_sv_eq_ci_lit(token, "GIT_SUBMODULES");
}

static bool fetchcontent_is_unsupported_transport_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "SVN_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "HG_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "CVS_REPOSITORY");
}

static bool fetchcontent_append_list_to_temp(Evaluator_Context *ctx, SV_List *out, const SV_List src) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < arena_arr_len(src); i++) {
        if (!eval_sv_arr_push_temp(ctx, out, src[i])) return false;
    }
    return true;
}

static bool fetchcontent_clone_list_to_event(Evaluator_Context *ctx, SV_List *out, const SV_List src) {
    if (!ctx || !out) return false;
    *out = NULL;
    for (size_t i = 0; i < arena_arr_len(src); i++) {
        String_View stable = sv_copy_to_event_arena(ctx, src[i]);
        if (eval_should_stop(ctx)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *out, stable)) return false;
    }
    return true;
}

static bool fetchcontent_clone_declaration_to_event(Evaluator_Context *ctx,
                                                    Eval_FetchContent_Declaration *out_decl,
                                                    const Eval_FetchContent_Declaration *src_decl) {
    if (!ctx || !out_decl || !src_decl) return false;
    memset(out_decl, 0, sizeof(*out_decl));

    *out_decl = *src_decl;
    out_decl->name = sv_copy_to_event_arena(ctx, src_decl->name);
    out_decl->canonical_name = sv_copy_to_event_arena(ctx, src_decl->canonical_name);
    out_decl->source_dir = sv_copy_to_event_arena(ctx, src_decl->source_dir);
    out_decl->binary_dir = sv_copy_to_event_arena(ctx, src_decl->binary_dir);
    out_decl->source_subdir = sv_copy_to_event_arena(ctx, src_decl->source_subdir);
    out_decl->url = sv_copy_to_event_arena(ctx, src_decl->url);
    out_decl->url_hash = sv_copy_to_event_arena(ctx, src_decl->url_hash);
    out_decl->url_md5 = sv_copy_to_event_arena(ctx, src_decl->url_md5);
    out_decl->git_repository = sv_copy_to_event_arena(ctx, src_decl->git_repository);
    out_decl->git_tag = sv_copy_to_event_arena(ctx, src_decl->git_tag);
    if (eval_should_stop(ctx)) return false;

    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->args, src_decl->args)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->find_package_args, src_decl->find_package_args)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->git_submodules, src_decl->git_submodules)) return false;
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool fetchcontent_try_find_mode_from_var(Evaluator_Context *ctx,
                                                Eval_FetchContent_Try_Find_Mode *out_mode) {
    if (!ctx || !out_mode) return false;
    String_View mode = eval_var_get_visible(ctx, nob_sv_from_cstr("FETCHCONTENT_TRY_FIND_PACKAGE_MODE"));
    if (eval_sv_eq_ci_lit(mode, "ALWAYS")) {
        *out_mode = EVAL_FETCHCONTENT_TRY_FIND_ALWAYS;
    } else if (eval_sv_eq_ci_lit(mode, "NEVER")) {
        *out_mode = EVAL_FETCHCONTENT_TRY_FIND_NEVER;
    } else {
        *out_mode = EVAL_FETCHCONTENT_TRY_FIND_OPT_IN;
    }
    return true;
}

static bool fetchcontent_store_single_value(Evaluator_Context *ctx,
                                            const Node *node,
                                            String_View dependency_name,
                                            String_View keyword,
                                            String_View value,
                                            Eval_FetchContent_Declaration *decl,
                                            bool saved_details_defaults) {
    (void)dependency_name;
    if (!ctx || !node || !decl) return false;

    if (eval_sv_eq_ci_lit(keyword, "SOURCE_DIR")) {
        value = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_source_dir(ctx), false);
        decl->source_dir = value;
        decl->has_source_dir = true;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(keyword, "BINARY_DIR")) {
        value = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_binary_dir(ctx), false);
        decl->binary_dir = value;
        decl->has_binary_dir = true;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(keyword, "SOURCE_SUBDIR")) {
        decl->source_subdir = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "URL")) {
        if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_GIT) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Declare() may not mix URL and GIT transports"),
                                           dependency_name);
            return false;
        }
        decl->transport = EVAL_FETCHCONTENT_TRANSPORT_URL;
        decl->url = fetchcontent_resolve_local_like_path(ctx, value, fetchcontent_current_source_dir(ctx));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(keyword, "URL_HASH")) {
        decl->transport = decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                              ? EVAL_FETCHCONTENT_TRANSPORT_URL
                              : decl->transport;
        decl->url_hash = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "URL_MD5")) {
        decl->transport = decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                              ? EVAL_FETCHCONTENT_TRANSPORT_URL
                              : decl->transport;
        decl->url_md5 = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_REPOSITORY")) {
        if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_URL) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Declare() may not mix URL and GIT transports"),
                                           dependency_name);
            return false;
        }
        decl->transport = EVAL_FETCHCONTENT_TRANSPORT_GIT;
        decl->git_repository = fetchcontent_resolve_local_like_path(ctx, value, fetchcontent_current_source_dir(ctx));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_TAG")) {
        decl->transport = decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                              ? EVAL_FETCHCONTENT_TRANSPORT_GIT
                              : decl->transport;
        decl->git_tag = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SUBBUILD_DIR")) {
        (void)saved_details_defaults;
        return true;
    }
    return true;
}

static bool fetchcontent_parse_declaration(Evaluator_Context *ctx,
                                           const Node *node,
                                           String_View dependency_name,
                                           const SV_List args,
                                           bool saved_details_defaults,
                                           Eval_FetchContent_Declaration *out_decl) {
    if (!ctx || !node || !out_decl) return false;
    memset(out_decl, 0, sizeof(*out_decl));

    out_decl->name = dependency_name;
    out_decl->canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    out_decl->download_extract_timestamp = true;
    out_decl->try_find_mode = EVAL_FETCHCONTENT_TRY_FIND_OPT_IN;
    if (!fetchcontent_try_find_mode_from_var(ctx, &out_decl->try_find_mode)) return false;
    out_decl->find_package_targets_global =
        eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_FIND_PACKAGE_TARGETS_GLOBAL")));
    if (saved_details_defaults) {
        out_decl->source_dir = fetchcontent_saved_default_source_dir(ctx, out_decl->canonical_name);
        out_decl->binary_dir = fetchcontent_saved_default_binary_dir(ctx, out_decl->canonical_name);
    } else {
        out_decl->source_dir = fetchcontent_direct_default_source_dir(ctx, out_decl->canonical_name);
        out_decl->binary_dir = fetchcontent_direct_default_binary_dir(ctx, out_decl->canonical_name);
    }
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View token = args[i];
        if (!eval_sv_arr_push_temp(ctx, &out_decl->args, token)) return false;

        if (fetchcontent_is_unsupported_transport_key(token)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_NOT_IMPLEMENTED,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent transport is not supported in this wave"),
                                           token);
            return false;
        }

        if (fetchcontent_is_flag_key(token)) {
            if (eval_sv_eq_ci_lit(token, "EXCLUDE_FROM_ALL")) out_decl->exclude_from_all = true;
            else if (eval_sv_eq_ci_lit(token, "SYSTEM")) out_decl->system = true;
            else if (eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE")) out_decl->override_find_package = true;
            continue;
        }

        if (fetchcontent_is_bool_value_key(token)) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               eval_origin_from_node(ctx, node),
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_MISSING_REQUIRED,
                                               "dispatcher",
                                               nob_sv_from_cstr("FetchContent option requires a value"),
                                               token);
                return false;
            }
            String_View value = args[++i];
            if (!eval_sv_arr_push_temp(ctx, &out_decl->args, value)) return false;
            bool truthy = eval_truthy(ctx, value);
            if (eval_sv_eq_ci_lit(token, "DOWNLOAD_NO_EXTRACT")) {
                out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                          ? EVAL_FETCHCONTENT_TRANSPORT_URL
                                          : out_decl->transport;
                out_decl->download_no_extract = truthy;
            } else if (eval_sv_eq_ci_lit(token, "DOWNLOAD_EXTRACT_TIMESTAMP")) {
                out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                          ? EVAL_FETCHCONTENT_TRANSPORT_URL
                                          : out_decl->transport;
                out_decl->download_extract_timestamp = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_SHALLOW")) {
                out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                          ? EVAL_FETCHCONTENT_TRANSPORT_GIT
                                          : out_decl->transport;
                out_decl->git_shallow = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_PROGRESS")) {
                out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                          ? EVAL_FETCHCONTENT_TRANSPORT_GIT
                                          : out_decl->transport;
                out_decl->git_progress = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_SUBMODULES_RECURSE")) {
                out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                          ? EVAL_FETCHCONTENT_TRANSPORT_GIT
                                          : out_decl->transport;
                out_decl->git_submodules_recurse = truthy;
            }
            continue;
        }

        if (fetchcontent_is_single_value_key(token)) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               eval_origin_from_node(ctx, node),
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_MISSING_REQUIRED,
                                               "dispatcher",
                                               nob_sv_from_cstr("FetchContent option requires a value"),
                                               token);
                return false;
            }
            String_View value = args[++i];
            if (!eval_sv_arr_push_temp(ctx, &out_decl->args, value)) return false;
            if (!fetchcontent_store_single_value(ctx, node, dependency_name, token, value, out_decl, saved_details_defaults)) {
                return false;
            }
            if (eval_should_stop(ctx)) return false;
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "GIT_SUBMODULES")) {
            out_decl->transport = out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE
                                      ? EVAL_FETCHCONTENT_TRANSPORT_GIT
                                      : out_decl->transport;
            out_decl->has_git_submodules = true;
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_flag_key(args[j]) ||
                    fetchcontent_is_bool_value_key(args[j]) ||
                    fetchcontent_is_single_value_key(args[j]) ||
                    fetchcontent_is_multi_value_key(args[j])) {
                    break;
                }
                if (!eval_sv_arr_push_temp(ctx, &out_decl->git_submodules, args[j])) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                i = j;
            }
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "FIND_PACKAGE_ARGS")) {
            out_decl->has_find_package_args = true;
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (!eval_sv_arr_push_temp(ctx, &out_decl->find_package_args, args[j])) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                if (eval_sv_eq_ci_lit(args[j], "OVERRIDE_FIND_PACKAGE")) out_decl->override_find_package = true;
            }
            break;
        }
    }

    if (out_decl->override_find_package && out_decl->has_find_package_args) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_INVALID_VALUE,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_Declare() may not combine FIND_PACKAGE_ARGS with OVERRIDE_FIND_PACKAGE"),
                                       dependency_name);
        return false;
    }

    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_URL) {
        if (out_decl->url.count == 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_MISSING_REQUIRED,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent URL transport requires URL"),
                                           dependency_name);
            return false;
        }
        if (out_decl->url_hash.count > 0 && out_decl->url_md5.count > 0) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent URL transport may not combine URL_HASH with URL_MD5"),
                                           dependency_name);
            return false;
        }
    }
    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_GIT && out_decl->git_repository.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent GIT transport requires GIT_REPOSITORY"),
                                       dependency_name);
        return false;
    }
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
        if (!eval_sv_arr_push_temp(ctx, out_args, decl->source_dir)) return false;
    }
    if (!saw_binary_dir) {
        if (!eval_sv_arr_push_temp(ctx, out_args, nob_sv_from_cstr("BINARY_DIR"))) return false;
        if (!eval_sv_arr_push_temp(ctx, out_args, decl->binary_dir)) return false;
    }
    return true;
}

static bool fetchcontent_run_inline_command(Evaluator_Context *ctx,
                                            String_View command_name,
                                            const SV_List *args) {
    if (!ctx || command_name.count == 0 || !args) return false;
    String_View script = nob_sv_from_cstr("");
    Ast_Root ast = NULL;
    if (!flow_build_call_script(ctx, command_name, args, &script)) return false;
    if (!flow_parse_inline_script(ctx, script, &ast)) return false;
    Eval_Result res = eval_run_ast_inline(ctx, ast);
    return !eval_result_is_fatal(res) && !eval_result_is_soft_error(res);
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
    return fetchcontent_run_inline_command(ctx, nob_sv_from_cstr("add_subdirectory"), &call_args);
}

static bool fetchcontent_run_file_download(Evaluator_Context *ctx,
                                           String_View input,
                                           String_View output,
                                           String_View expected_hash,
                                           String_View expected_md5) {
    if (!ctx || input.count == 0 || output.count == 0) return false;
    SV_List args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("DOWNLOAD"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, input)) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, output)) return false;
    if (expected_hash.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("EXPECTED_HASH"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, expected_hash)) return false;
    }
    if (expected_md5.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("EXPECTED_MD5"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, expected_md5)) return false;
    }
    return fetchcontent_run_inline_command(ctx, nob_sv_from_cstr("file"), &args);
}

static bool fetchcontent_run_file_archive_extract(Evaluator_Context *ctx,
                                                  String_View input,
                                                  String_View destination,
                                                  bool preserve_timestamps) {
    if (!ctx || input.count == 0 || destination.count == 0) return false;
    SV_List args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("ARCHIVE_EXTRACT"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("INPUT"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, input)) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("DESTINATION"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, destination)) return false;
    if (!preserve_timestamps && !eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("TOUCH"))) return false;
    return fetchcontent_run_inline_command(ctx, nob_sv_from_cstr("file"), &args);
}

static bool fetchcontent_run_find_package(Evaluator_Context *ctx,
                                          String_View dependency_name,
                                          const Eval_FetchContent_Declaration *decl,
                                          bool *out_found) {
    if (!ctx || !decl || !out_found) return false;
    *out_found = false;

    bool allow_try_find = false;
    if (decl->try_find_mode == EVAL_FETCHCONTENT_TRY_FIND_ALWAYS) allow_try_find = true;
    else if (decl->try_find_mode == EVAL_FETCHCONTENT_TRY_FIND_OPT_IN && decl->has_find_package_args) allow_try_find = true;
    if (!allow_try_find) return true;

    SV_List args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &args, dependency_name)) return false;
    if (!fetchcontent_append_list_to_temp(ctx, &args, decl->find_package_args)) return false;
    if (decl->find_package_targets_global) {
        bool has_global = false;
        for (size_t i = 1; i < arena_arr_len(args); i++) {
            if (eval_sv_eq_ci_lit(args[i], "GLOBAL")) {
                has_global = true;
                break;
            }
        }
        if (!has_global && !eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("GLOBAL"))) return false;
    }

    if (!fetchcontent_run_inline_command(ctx, nob_sv_from_cstr("find_package"), &args)) return false;

    String_View found_key = svu_concat_suffix_temp(ctx, dependency_name, "_FOUND");
    if (eval_should_stop(ctx)) return false;
    *out_found = eval_var_defined_visible(ctx, found_key) && eval_truthy(ctx, eval_var_get_visible(ctx, found_key));
    return true;
}

static String_View fetchcontent_path_basename_temp(Evaluator_Context *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("content");
    size_t start = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
    }
    if (start >= path.count) return nob_sv_from_cstr("content");
    return nob_sv_from_parts(path.data + start, path.count - start);
}

static bool fetchcontent_git_run(Evaluator_Context *ctx,
                                 String_View working_directory,
                                 const SV_List argv,
                                 String_View *out_stderr,
                                 int *out_exit_code) {
    if (!ctx) return false;
    Eval_Process_Run_Request req = {0};
    req.argv = (String_View*)argv;
    req.argc = arena_arr_len(argv);
    req.working_directory = working_directory;

    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &req, &proc)) return false;
    if (out_stderr) *out_stderr = proc.stderr_text;
    if (out_exit_code) *out_exit_code = proc.exit_code;
    return true;
}

static bool fetchcontent_git_emit_failure(Evaluator_Context *ctx,
                                          const Node *node,
                                          String_View cause,
                                          String_View detail) {
    if (!ctx || !node) return false;
    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                   node,
                                   eval_origin_from_node(ctx, node),
                                   EV_DIAG_ERROR,
                                   EVAL_DIAG_IO_FAILURE,
                                   "dispatcher",
                                   cause,
                                   detail);
    return false;
}

static bool fetchcontent_git_update_submodules(Evaluator_Context *ctx,
                                               const Node *node,
                                               const Eval_FetchContent_Declaration *decl,
                                               String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    if (!decl->has_git_submodules && !decl->git_submodules_recurse) return true;
    if (decl->has_git_submodules &&
        arena_arr_len(decl->git_submodules) == 1 &&
        decl->git_submodules[0].count == 0) {
        return true;
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("submodule"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("update"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--init"))) return false;
    if (decl->git_submodules_recurse && !eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--recursive"))) return false;
    if (decl->has_git_submodules && arena_arr_len(decl->git_submodules) > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--"))) return false;
        if (!fetchcontent_append_list_to_temp(ctx, &argv, decl->git_submodules)) return false;
    }

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_git_run(ctx, source_dir, argv, &stderr_text, &exit_code)) return false;
    if (exit_code != 0) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent Git submodule update failed"),
                                             stderr_text.count > 0 ? stderr_text : source_dir);
    }
    return true;
}

static bool fetchcontent_git_checkout(Evaluator_Context *ctx,
                                      const Node *node,
                                      String_View source_dir,
                                      String_View git_tag) {
    if (!ctx || !node || git_tag.count == 0) return false;
    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("checkout"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, git_tag)) return false;

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_git_run(ctx, source_dir, argv, &stderr_text, &exit_code)) return false;
    if (exit_code != 0) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent Git checkout failed"),
                                             stderr_text.count > 0 ? stderr_text : git_tag);
    }
    return true;
}

static bool fetchcontent_git_fetch_and_update(Evaluator_Context *ctx,
                                              const Node *node,
                                              const Eval_FetchContent_Declaration *decl,
                                              String_View source_dir,
                                              bool disconnected) {
    if (!ctx || !node || !decl) return false;
    if (disconnected) return true;

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("fetch"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--tags"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("origin"))) return false;

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_git_run(ctx, source_dir, argv, &stderr_text, &exit_code)) return false;
    if (exit_code != 0) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent Git fetch failed"),
                                             stderr_text.count > 0 ? stderr_text : source_dir);
    }

    if (decl->git_tag.count > 0) {
        if (!fetchcontent_git_checkout(ctx, node, source_dir, decl->git_tag)) return false;
    } else {
        SV_List pull_argv = NULL;
        if (!eval_sv_arr_push_temp(ctx, &pull_argv, nob_sv_from_cstr("git"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &pull_argv, nob_sv_from_cstr("pull"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &pull_argv, nob_sv_from_cstr("--ff-only"))) return false;
        stderr_text = nob_sv_from_cstr("");
        exit_code = 0;
        if (!fetchcontent_git_run(ctx, source_dir, pull_argv, &stderr_text, &exit_code)) return false;
        if (exit_code != 0) {
            return fetchcontent_git_emit_failure(ctx,
                                                 node,
                                                 nob_sv_from_cstr("FetchContent Git update failed"),
                                                 stderr_text.count > 0 ? stderr_text : source_dir);
        }
    }

    return fetchcontent_git_update_submodules(ctx, node, decl, source_dir);
}

static bool fetchcontent_is_updates_disconnected(Evaluator_Context *ctx, String_View dependency_name) {
    if (!ctx) return false;
    String_View upper = fetchcontent_upper_temp(ctx, dependency_name);
    String_View key_parts[2] = {
        nob_sv_from_cstr("FETCHCONTENT_UPDATES_DISCONNECTED_"),
        upper,
    };
    String_View key = svu_join_no_sep_temp(ctx, key_parts, 2);
    if (eval_should_stop(ctx)) return false;

    if (eval_var_defined_visible(ctx, key)) return eval_truthy(ctx, eval_var_get_visible(ctx, key));
    return eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("FETCHCONTENT_UPDATES_DISCONNECTED")));
}

static bool fetchcontent_populate_url(Evaluator_Context *ctx,
                                      const Node *node,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    if (!fetchcontent_mkdir_p(ctx, source_dir)) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to create SOURCE_DIR"),
                                             source_dir);
    }

    String_View archive_name = fetchcontent_path_basename_temp(ctx, decl->url);
    String_View archive_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, archive_name);
    if (eval_should_stop(ctx)) return false;

    if (!fetchcontent_run_file_download(ctx, decl->url, archive_path, decl->url_hash, decl->url_md5)) return false;
    if (decl->download_no_extract) return true;
    return fetchcontent_run_file_archive_extract(ctx,
                                                 archive_path,
                                                 source_dir,
                                                 decl->download_extract_timestamp);
}

static bool fetchcontent_populate_git(Evaluator_Context *ctx,
                                      const Node *node,
                                      String_View dependency_name,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    bool disconnected = fetchcontent_is_updates_disconnected(ctx, dependency_name);
    String_View git_dir = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".git"));
    if (eval_should_stop(ctx)) return false;

    if (fetchcontent_file_exists(ctx, git_dir)) {
        return fetchcontent_git_fetch_and_update(ctx, node, decl, source_dir, disconnected);
    }

    String_View parent_marker = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".clone"));
    if (eval_should_stop(ctx)) return false;
    if (!eval_mkdirs_for_parent(ctx, parent_marker)) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to prepare Git destination"),
                                             source_dir);
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("clone"))) return false;
    if (decl->git_shallow) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--depth"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("1"))) return false;
        if (decl->git_tag.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--branch"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, decl->git_tag)) return false;
        }
    }
    if (decl->git_progress && !eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--progress"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->git_repository)) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, source_dir)) return false;

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_git_run(ctx, nob_sv_from_cstr(""), argv, &stderr_text, &exit_code)) return false;
    if (exit_code != 0) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent Git clone failed"),
                                             stderr_text.count > 0 ? stderr_text : decl->git_repository);
    }

    if (decl->git_tag.count > 0 && !decl->git_shallow) {
        if (!fetchcontent_git_checkout(ctx, node, source_dir, decl->git_tag)) return false;
    }
    return fetchcontent_git_update_submodules(ctx, node, decl, source_dir);
}

static bool fetchcontent_write_redirect_stub(Evaluator_Context *ctx,
                                             String_View dependency_name,
                                             String_View redirect_path,
                                             bool version_file) {
    if (!ctx || dependency_name.count == 0 || redirect_path.count == 0) return false;
    Nob_String_Builder sb = {0};
    if (version_file) {
        nob_sb_append_cstr(&sb, "set(PACKAGE_VERSION \"0\")\n");
        nob_sb_append_cstr(&sb, "set(PACKAGE_VERSION_COMPATIBLE 1)\n");
        nob_sb_append_cstr(&sb, "set(PACKAGE_VERSION_EXACT 0)\n");
    } else {
        nob_sb_appendf(&sb, "FetchContent_MakeAvailable(%.*s)\n", (int)dependency_name.count, dependency_name.data);
        nob_sb_appendf(&sb,
                       "FetchContent_GetProperties(%.*s SOURCE_DIR %.*s_SOURCE_DIR BINARY_DIR %.*s_BINARY_DIR POPULATED %.*s_POPULATED)\n",
                       (int)dependency_name.count,
                       dependency_name.data,
                       (int)dependency_name.count,
                       dependency_name.data,
                       (int)dependency_name.count,
                       dependency_name.data,
                       (int)dependency_name.count,
                       dependency_name.data);
        nob_sb_appendf(&sb, "set(%.*s_FOUND 1)\n", (int)dependency_name.count, dependency_name.data);
        nob_sb_appendf(&sb, "set(%.*s_DIR \"${%.*s_SOURCE_DIR}\")\n",
                       (int)dependency_name.count,
                       dependency_name.data,
                       (int)dependency_name.count,
                       dependency_name.data);
    }
    String_View contents = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
    bool ok = eval_write_text_file(ctx, redirect_path, contents, false);
    nob_sb_free(sb);
    return ok;
}

static bool fetchcontent_stage_override_redirects(Evaluator_Context *ctx,
                                                  const Eval_FetchContent_Declaration *decl) {
    if (!ctx || !decl || !decl->override_find_package) return false;
    String_View redirects_dir = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_FIND_PACKAGE_REDIRECTS_DIR"));
    if (redirects_dir.count == 0) return false;

    String_View config_name = svu_concat_suffix_temp(ctx, decl->name, "Config.cmake");
    String_View version_name = svu_concat_suffix_temp(ctx, decl->name, "ConfigVersion.cmake");
    String_View lower = fetchcontent_lower_temp(ctx, decl->name);
    String_View lower_config = svu_concat_suffix_temp(ctx, lower, "-config.cmake");
    String_View lower_version = svu_concat_suffix_temp(ctx, lower, "-config-version.cmake");
    if (eval_should_stop(ctx)) return false;

    String_View paths[4] = {
        eval_sv_path_join(eval_temp_arena(ctx), redirects_dir, config_name),
        eval_sv_path_join(eval_temp_arena(ctx), redirects_dir, version_name),
        eval_sv_path_join(eval_temp_arena(ctx), redirects_dir, lower_config),
        eval_sv_path_join(eval_temp_arena(ctx), redirects_dir, lower_version),
    };
    if (eval_should_stop(ctx)) return false;

    if (!fetchcontent_write_redirect_stub(ctx, decl->name, paths[0], false)) return false;
    if (!fetchcontent_write_redirect_stub(ctx, decl->name, paths[1], true)) return false;
    if (!fetchcontent_write_redirect_stub(ctx, decl->name, paths[2], false)) return false;
    if (!fetchcontent_write_redirect_stub(ctx, decl->name, paths[3], true)) return false;
    return true;
}

static bool fetchcontent_finalize_population(Evaluator_Context *ctx,
                                             const Eval_FetchContent_Declaration *decl,
                                             bool populated,
                                             String_View source_dir,
                                             String_View binary_dir) {
    if (!ctx || !decl) return false;
    if (!fetchcontent_upsert_state(ctx, decl->name, decl->canonical_name, populated, source_dir, binary_dir)) return false;
    if (!fetchcontent_publish_default_vars(ctx, decl->name, populated, source_dir, binary_dir)) return false;
    if (populated && decl->override_find_package && !fetchcontent_stage_override_redirects(ctx, decl)) return false;
    return true;
}

static bool fetchcontent_populate_content(Evaluator_Context *ctx,
                                          const Node *node,
                                          String_View dependency_name,
                                          const Eval_FetchContent_Declaration *decl,
                                          bool run_add_subdirectory) {
    if (!ctx || !node || !decl) return false;

    String_View source_dir = decl->source_dir;
    String_View binary_dir = decl->binary_dir;
    String_View upper = fetchcontent_upper_temp(ctx, dependency_name);
    String_View override_key_parts[2] = {
        nob_sv_from_cstr("FETCHCONTENT_SOURCE_DIR_"),
        upper,
    };
    String_View override_key = svu_join_no_sep_temp(ctx, override_key_parts, 2);
    if (eval_should_stop(ctx)) return false;

    String_View override_source_dir = eval_var_get_visible(ctx, override_key);
    if (override_source_dir.count > 0) {
        source_dir = eval_path_resolve_for_cmake_arg(ctx, override_source_dir, fetchcontent_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return false;
    }

    if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE && !decl->has_source_dir && override_source_dir.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent population requires SOURCE_DIR, URL, GIT_REPOSITORY, provider fulfillment, or a package resolution"),
                                       dependency_name);
        return false;
    }

    if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_URL) {
        if (!fetchcontent_populate_url(ctx, node, decl, source_dir)) return false;
    } else if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_GIT) {
        if (!fetchcontent_populate_git(ctx, node, dependency_name, decl, source_dir)) return false;
    }

    if (!fetchcontent_finalize_population(ctx, decl, true, source_dir, binary_dir)) return false;

    if (!run_add_subdirectory) return true;
    String_View add_subdirectory_source = source_dir;
    if (decl->source_subdir.count > 0) {
        add_subdirectory_source = eval_sv_path_join(eval_temp_arena(ctx), source_dir, decl->source_subdir);
        if (eval_should_stop(ctx)) return false;
    }
    String_View cmakelists = eval_sv_path_join(eval_temp_arena(ctx), add_subdirectory_source, nob_sv_from_cstr("CMakeLists.txt"));
    if (eval_should_stop(ctx)) return false;
    if (fetchcontent_file_exists(ctx, cmakelists)) {
        if (!fetchcontent_run_add_subdirectory(ctx,
                                               add_subdirectory_source,
                                               binary_dir,
                                               decl->exclude_from_all,
                                               decl->system)) {
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

    String_View provider_command_name = ctx->semantic_state.package.dependency_provider.command_name;
    if (provider_command_name.count == 0 ||
        !ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial) {
        return true;
    }

    SV_List provider_method_args = NULL;
    SV_List provider_args = NULL;
    if (!fetchcontent_build_provider_args(ctx, decl, &provider_method_args)) return false;
    if (!eval_sv_arr_push_temp(ctx, &provider_args, nob_sv_from_cstr("FETCHCONTENT_MAKEAVAILABLE_SERIAL"))) return false;
    for (size_t i = 0; i < arena_arr_len(provider_method_args); i++) {
        if (!eval_sv_arr_push_temp(ctx, &provider_args, provider_method_args[i])) return false;
    }

    ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth++;
    Eval_Result invoke_result = eval_result_ok();
    if (!eval_user_cmd_invoke(ctx, provider_command_name, &provider_args, eval_origin_from_node(ctx, node))) {
        invoke_result = eval_result_from_ctx(ctx);
    } else {
        invoke_result = eval_result_ok_if_running(ctx);
    }
    if (ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth > 0) {
        ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth--;
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

    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, canonical_name);
    if (state && state->populated) {
        bool ok = fetchcontent_publish_default_vars(ctx,
                                                    state->name.count > 0 ? state->name : dependency_name,
                                                    true,
                                                    state->source_dir,
                                                    state->binary_dir);
        fetchcontent_pop_active(ctx, pushed_active);
        return ok;
    }

    Eval_FetchContent_Declaration *decl = fetchcontent_find_declaration(ctx, canonical_name);
    if (!decl) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_MakeAvailable() requires a prior FetchContent_Declare()"),
                                       dependency_name);
        fetchcontent_pop_active(ctx, pushed_active);
        return false;
    }

    bool provider_populated = false;
    if (!already_active &&
        !has_source_override &&
        ctx->semantic_state.package.dependency_provider.command_name.count > 0 &&
        ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial) {
        if (!fetchcontent_invoke_dependency_provider(ctx, node, decl, &provider_populated)) {
            fetchcontent_pop_active(ctx, pushed_active);
            return false;
        }
    }

    if (provider_populated) {
        Eval_FetchContent_State *provider_state = fetchcontent_find_state(ctx, canonical_name);
        bool ok = provider_state &&
                  fetchcontent_publish_default_vars(ctx,
                                                    dependency_name,
                                                    true,
                                                    provider_state->source_dir,
                                                    provider_state->binary_dir);
        fetchcontent_pop_active(ctx, pushed_active);
        return ok;
    }

    bool found_package = false;
    if (!fetchcontent_run_find_package(ctx, dependency_name, decl, &found_package)) {
        fetchcontent_pop_active(ctx, pushed_active);
        return false;
    }
    if (found_package) {
        Eval_FetchContent_State *decl_state = fetchcontent_find_state(ctx, canonical_name);
        String_View source_dir = decl_state ? decl_state->source_dir : decl->source_dir;
        String_View binary_dir = decl_state ? decl_state->binary_dir : decl->binary_dir;
        bool ok = fetchcontent_finalize_population(ctx, decl, false, source_dir, binary_dir);
        fetchcontent_pop_active(ctx, pushed_active);
        return ok;
    }

    bool ok = fetchcontent_populate_content(ctx, node, dependency_name, decl, true);
    fetchcontent_pop_active(ctx, pushed_active);
    return ok;
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

    SV_List decl_args = NULL;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (!eval_sv_arr_push_temp(ctx, &decl_args, a[i])) return eval_result_fatal();
    }
    Eval_FetchContent_Declaration parsed = {0};
    if (!fetchcontent_parse_declaration(ctx, node, dependency_name, decl_args, true, &parsed)) {
        return eval_result_from_ctx(ctx);
    }

    Eval_FetchContent_Declaration decl = {0};
    if (!fetchcontent_clone_declaration_to_event(ctx, &decl, &parsed)) return eval_result_fatal();
    if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.fetchcontent.declarations, decl)) {
        return eval_result_fatal();
    }
    if (!fetchcontent_upsert_state(ctx, decl.name, decl.canonical_name, false, decl.source_dir, decl.binary_dir)) {
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

Eval_Result eval_handle_fetchcontent_populate(Evaluator_Context *ctx, const Node *node) {
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
                                       nob_sv_from_cstr("FetchContent_Populate() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_Populate(<name> [<content options>...])"));
        return eval_result_from_ctx(ctx);
    }

    String_View dependency_name = a[0];
    String_View canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Eval_FetchContent_Declaration direct_decl = {0};
    const Eval_FetchContent_Declaration *decl = NULL;
    if (arena_arr_len(a) == 1) {
        decl = fetchcontent_find_declaration(ctx, canonical_name);
        if (!decl) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           origin,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_MISSING_REQUIRED,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Populate() requires a prior FetchContent_Declare() when called without content options"),
                                           dependency_name);
            return eval_result_from_ctx(ctx);
        }
    } else {
        SV_List decl_args = NULL;
        for (size_t i = 1; i < arena_arr_len(a); i++) {
            if (!eval_sv_arr_push_temp(ctx, &decl_args, a[i])) return eval_result_fatal();
        }
        if (!fetchcontent_parse_declaration(ctx, node, dependency_name, decl_args, false, &direct_decl)) {
            return eval_result_from_ctx(ctx);
        }
        decl = &direct_decl;
    }

    if (!fetchcontent_populate_content(ctx, node, dependency_name, decl, false)) {
        return eval_result_from_ctx(ctx);
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
    if (ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth == 0 ||
        arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) == 0) {
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
        ctx->semantic_state.fetchcontent.active_makeavailable[arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) - 1];
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
        if (source_dir.count == 0) source_dir = decl->source_dir;
        if (binary_dir.count == 0) binary_dir = decl->binary_dir;
    } else {
        if (source_dir.count == 0) source_dir = fetchcontent_saved_default_source_dir(ctx, canonical_name);
        if (binary_dir.count == 0) binary_dir = fetchcontent_saved_default_binary_dir(ctx, canonical_name);
    }
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (!fetchcontent_upsert_state(ctx, dependency_name, canonical_name, true, source_dir, binary_dir)) return eval_result_fatal();
    if (!fetchcontent_publish_default_vars(ctx, dependency_name, true, source_dir, binary_dir)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
