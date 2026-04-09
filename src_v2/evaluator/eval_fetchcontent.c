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

typedef struct {
    String_View dependency_name;
    String_View canonical_name;
    bool skip_existing;
    Eval_FetchContent_Declaration parsed_decl;
} FetchContent_Declare_Request;

typedef struct {
    SV_List dependency_names;
} FetchContent_MakeAvailable_Request;

typedef struct {
    String_View dependency_name;
    String_View canonical_name;
    FetchContent_GetProperties_Options outputs;
} FetchContent_GetProperties_Request;

typedef struct {
    String_View dependency_name;
    String_View canonical_name;
    bool use_saved_declaration;
    Eval_FetchContent_Declaration direct_decl;
} FetchContent_Populate_Request;

typedef struct {
    String_View dependency_name;
    String_View canonical_name;
    String_View source_dir;
    String_View binary_dir;
} FetchContent_SetPopulated_Request;

static bool fetchcontent_git_emit_failure(EvalExecContext *ctx,
                                          const Node *node,
                                          String_View cause,
                                          String_View detail);
static String_View fetchcontent_current_source_dir(EvalExecContext *ctx);
static bool fetchcontent_looks_like_url(String_View value);

static bool fetchcontent_parse_expected_hash(String_View in,
                                             String_View *out_algo,
                                             String_View *out_hex) {
    if (!out_algo || !out_hex) return false;
    *out_algo = nob_sv_from_cstr("");
    *out_hex = nob_sv_from_cstr("");
    for (size_t i = 0; i < in.count; ++i) {
        if (in.data[i] != '=') continue;
        *out_algo = nob_sv_from_parts(in.data, i);
        *out_hex = nob_sv_from_parts(in.data + i + 1, in.count - i - 1);
        return out_algo->count > 0 && out_hex->count > 0;
    }
    return false;
}

static String_View fetchcontent_strip_file_scheme_temp(EvalExecContext *ctx, String_View value) {
    if (!ctx) return nob_sv_from_cstr("");
    if (value.count >= 7 &&
        (memcmp(value.data, "file://", 7) == 0 || memcmp(value.data, "FILE://", 7) == 0)) {
        return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(value.data + 7, value.count - 7));
    }
    return value;
}

static bool fetchcontent_emit_replay_marker(EvalExecContext *ctx,
                                            Cmake_Event_Origin origin) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    return eval_begin_replay_action(ctx,
                                    origin,
                                    EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
                                    EVENT_REPLAY_OPCODE_NONE,
                                    EVENT_REPLAY_PHASE_CONFIGURE,
                                    eval_current_binary_dir(ctx),
                                    &action_key);
}

static bool fetchcontent_emit_replay_source_dir(EvalExecContext *ctx,
                                                Cmake_Event_Origin origin,
                                                String_View dependency_name,
                                                String_View source_dir,
                                                String_View binary_dir) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
                                  EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, source_dir) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, binary_dir) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, dependency_name)) {
        return false;
    }
    return true;
}

static bool fetchcontent_emit_replay_local_archive(EvalExecContext *ctx,
                                                   Cmake_Event_Origin origin,
                                                   String_View dependency_name,
                                                   String_View archive_path,
                                                   String_View source_dir,
                                                   String_View binary_dir,
                                                   String_View hash_algo,
                                                   String_View hash_digest,
                                                   bool preserve_timestamps) {
    String_View action_key = nob_sv_from_cstr("");
    if (!ctx) return false;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
                                  EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  eval_current_binary_dir(ctx),
                                  &action_key) ||
        !eval_emit_replay_action_add_input(ctx, origin, action_key, archive_path) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, source_dir) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, binary_dir) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, dependency_name) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 1, hash_algo) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 2, hash_digest) ||
        !eval_emit_replay_action_add_argv(ctx,
                                          origin,
                                          action_key,
                                          3,
                                          preserve_timestamps ? nob_sv_from_cstr("1")
                                                              : nob_sv_from_cstr("0"))) {
        return false;
    }
    return true;
}

static bool fetchcontent_emit_replay_materialization(EvalExecContext *ctx,
                                                     Cmake_Event_Origin origin,
                                                     String_View dependency_name,
                                                     const Eval_FetchContent_Declaration *decl,
                                                     String_View source_dir,
                                                     String_View binary_dir,
                                                     bool has_source_override,
                                                     bool resolved_by_provider,
                                                     bool resolved_by_find_package) {
    if (!ctx || !decl) return false;
    if (resolved_by_provider || resolved_by_find_package) {
        return fetchcontent_emit_replay_marker(ctx, origin);
    }

    if (!decl->has_download_command &&
        !decl->has_patch_command &&
        !decl->has_update_command &&
        decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE &&
        (decl->has_source_dir || has_source_override)) {
        return fetchcontent_emit_replay_source_dir(ctx,
                                                   origin,
                                                   dependency_name,
                                                   source_dir,
                                                   binary_dir);
    }

    if (!decl->has_download_command &&
        !decl->has_patch_command &&
        !decl->has_update_command &&
        decl->transport == EVAL_FETCHCONTENT_TRANSPORT_URL &&
        arena_arr_len(decl->urls) == 1 &&
        !decl->download_no_extract &&
        decl->url_md5.count == 0) {
        String_View hash_algo = nob_sv_from_cstr("");
        String_View hash_digest = nob_sv_from_cstr("");
        String_View archive_path = fetchcontent_strip_file_scheme_temp(ctx, decl->urls[0]);
        bool local_archive = !fetchcontent_looks_like_url(archive_path);
        bool hash_ok = decl->url_hash.count == 0 ||
                       (fetchcontent_parse_expected_hash(decl->url_hash, &hash_algo, &hash_digest) &&
                        eval_sv_eq_ci_lit(hash_algo, "SHA256"));
        if (local_archive && hash_ok) {
            archive_path = eval_path_resolve_for_cmake_arg(ctx,
                                                           archive_path,
                                                           fetchcontent_current_source_dir(ctx),
                                                           false);
            if (eval_should_stop(ctx)) return false;
            return fetchcontent_emit_replay_local_archive(ctx,
                                                          origin,
                                                          dependency_name,
                                                          archive_path,
                                                          source_dir,
                                                          binary_dir,
                                                          hash_algo,
                                                          hash_digest,
                                                          decl->download_extract_timestamp);
        }
    }

    return fetchcontent_emit_replay_marker(ctx, origin);
}

static bool fetchcontent_require_module(EvalExecContext *ctx,
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

static String_View fetchcontent_lower_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(eval_temp_arena(ctx), in, tolower);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static String_View fetchcontent_lower_event(EvalExecContext *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(ctx->event_arena, in, tolower);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static String_View fetchcontent_upper_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx || in.count == 0) return nob_sv_from_cstr("");
    String_View out = fetchcontent_ascii_case_copy(eval_temp_arena(ctx), in, toupper);
    if (out.data == NULL) ctx_oom(ctx);
    return out;
}

static Eval_FetchContent_Declaration *fetchcontent_find_declaration(EvalExecContext *ctx,
                                                                    String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.declarations); i++) {
        Eval_FetchContent_Declaration *decl = &ctx->semantic_state.fetchcontent.declarations[i];
        if (eval_sv_key_eq(decl->canonical_name, canonical_name)) return decl;
    }
    return NULL;
}

static Eval_FetchContent_State *fetchcontent_find_state(EvalExecContext *ctx,
                                                        String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.states); i++) {
        Eval_FetchContent_State *state = &ctx->semantic_state.fetchcontent.states[i];
        if (eval_sv_key_eq(state->canonical_name, canonical_name)) return state;
    }
    return NULL;
}

static String_View fetchcontent_current_binary_dir(EvalExecContext *ctx) {
    String_View current = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    if (current.count == 0) current = ctx ? ctx->binary_dir : nob_sv_from_cstr("");
    return current;
}

static String_View fetchcontent_current_source_dir(EvalExecContext *ctx) {
    String_View current = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (current.count == 0) current = ctx ? ctx->source_dir : nob_sv_from_cstr("");
    return current;
}

static String_View fetchcontent_saved_base_dir(EvalExecContext *ctx) {
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

static String_View fetchcontent_saved_default_source_dir(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-src");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_saved_base_dir(ctx), suffix);
}

static String_View fetchcontent_saved_default_binary_dir(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-build");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_saved_base_dir(ctx), suffix);
}

static String_View fetchcontent_direct_default_source_dir(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-src");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_current_binary_dir(ctx), suffix);
}

static String_View fetchcontent_direct_default_binary_dir(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View suffix = svu_concat_suffix_temp(ctx, canonical_name, "-build");
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), fetchcontent_current_binary_dir(ctx), suffix);
}

static bool fetchcontent_file_exists(EvalExecContext *ctx, String_View path) {
    bool exists = false;
    return eval_service_file_exists(ctx, path, &exists) && exists;
}

static bool fetchcontent_mkdir_p(EvalExecContext *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return false;
    String_View marker = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr(".keep"));
    if (eval_should_stop(ctx)) return false;
    if (!eval_mkdirs_for_parent(ctx, marker)) return false;
    return eval_service_mkdir(ctx, dir);
}

static bool fetchcontent_push_active(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    String_View stable = sv_copy_to_event_arena(ctx, canonical_name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.fetchcontent.active_makeavailable, stable);
}

static void fetchcontent_pop_active(EvalExecContext *ctx, bool pushed) {
    if (!ctx || !pushed) return;
    if (arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) == 0) return;
    arena_arr_set_len(ctx->semantic_state.fetchcontent.active_makeavailable,
                      arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) - 1);
}

static bool fetchcontent_active_contains(EvalExecContext *ctx, String_View canonical_name) {
    if (!ctx || canonical_name.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable); i++) {
        if (eval_sv_key_eq(ctx->semantic_state.fetchcontent.active_makeavailable[i], canonical_name)) return true;
    }
    return false;
}

static bool fetchcontent_default_var_names(EvalExecContext *ctx,
                                           String_View dependency_name,
                                           String_View *out_source_var,
                                           String_View *out_binary_var,
                                           String_View *out_populated_var) {
    if (!ctx || dependency_name.count == 0) return false;
    String_View lower = fetchcontent_lower_temp(ctx, dependency_name);
    if (eval_should_stop(ctx)) return false;

    if (out_source_var) *out_source_var = svu_concat_suffix_temp(ctx, lower, "_SOURCE_DIR");
    if (out_binary_var) *out_binary_var = svu_concat_suffix_temp(ctx, lower, "_BINARY_DIR");
    if (out_populated_var) *out_populated_var = svu_concat_suffix_temp(ctx, lower, "_POPULATED");
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool fetchcontent_publish_default_vars(EvalExecContext *ctx,
                                              String_View dependency_name,
                                              bool publish_populated,
                                              bool populated,
                                              String_View source_dir,
                                              String_View binary_dir) {
    if (!ctx || dependency_name.count == 0) return false;

    String_View source_var = nob_sv_from_cstr("");
    String_View binary_var = nob_sv_from_cstr("");
    String_View populated_var = nob_sv_from_cstr("");
    if (!fetchcontent_default_var_names(ctx,
                                        dependency_name,
                                        &source_var,
                                        &binary_var,
                                        &populated_var)) {
        return false;
    }

    if (!eval_var_set_current(ctx, source_var, source_dir)) return false;
    if (!eval_var_set_current(ctx, binary_var, binary_dir)) return false;
    if (publish_populated &&
        !eval_var_set_current(ctx, populated_var, populated ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return false;
    }
    return true;
}

static bool fetchcontent_upsert_state(EvalExecContext *ctx,
                                      String_View dependency_name,
                                      String_View canonical_name,
                                      bool population_processed,
                                      bool populated,
                                      bool source_dir_known,
                                      String_View source_dir,
                                      bool binary_dir_known,
                                      String_View binary_dir,
                                      bool resolved_by_provider,
                                      bool resolved_by_find_package) {
    if (!ctx || dependency_name.count == 0 || canonical_name.count == 0) return false;
    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, canonical_name);
    if (state) {
        state->name = sv_copy_to_event_arena(ctx, dependency_name);
        state->population_processed = population_processed;
        state->populated = populated;
        state->source_dir_known = source_dir_known;
        state->binary_dir_known = binary_dir_known;
        state->resolved_by_provider = resolved_by_provider;
        state->resolved_by_find_package = resolved_by_find_package;
        state->source_dir = sv_copy_to_event_arena(ctx, source_dir);
        state->binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    Eval_FetchContent_State entry = {0};
    entry.name = sv_copy_to_event_arena(ctx, dependency_name);
    entry.canonical_name = sv_copy_to_event_arena(ctx, canonical_name);
    entry.population_processed = population_processed;
    entry.populated = populated;
    entry.source_dir_known = source_dir_known;
    entry.binary_dir_known = binary_dir_known;
    entry.resolved_by_provider = resolved_by_provider;
    entry.resolved_by_find_package = resolved_by_find_package;
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

static String_View fetchcontent_resolve_local_like_path(EvalExecContext *ctx,
                                                        String_View raw_value,
                                                        String_View base_dir) {
    if (!ctx || raw_value.count == 0) return nob_sv_from_cstr("");
    if (fetchcontent_looks_like_url(raw_value)) return raw_value;
    return eval_path_resolve_for_cmake_arg(ctx, raw_value, base_dir, false);
}

static bool fetchcontent_is_bool_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "DOWNLOAD_NO_EXTRACT") ||
           eval_sv_eq_ci_lit(token, "DOWNLOAD_EXTRACT_TIMESTAMP") ||
           eval_sv_eq_ci_lit(token, "DOWNLOAD_NO_PROGRESS") ||
           eval_sv_eq_ci_lit(token, "UPDATE_DISCONNECTED") ||
           eval_sv_eq_ci_lit(token, "GIT_SHALLOW") ||
           eval_sv_eq_ci_lit(token, "GIT_PROGRESS") ||
           eval_sv_eq_ci_lit(token, "GIT_SUBMODULES_RECURSE") ||
           eval_sv_eq_ci_lit(token, "TLS_VERIFY") ||
           eval_sv_eq_ci_lit(token, "SVN_TRUST_CERT");
}

static bool fetchcontent_is_single_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "SOURCE_DIR") ||
           eval_sv_eq_ci_lit(token, "BINARY_DIR") ||
           eval_sv_eq_ci_lit(token, "SUBBUILD_DIR") ||
           eval_sv_eq_ci_lit(token, "SOURCE_SUBDIR") ||
           eval_sv_eq_ci_lit(token, "URL_HASH") ||
           eval_sv_eq_ci_lit(token, "URL_MD5") ||
           eval_sv_eq_ci_lit(token, "DOWNLOAD_NAME") ||
           eval_sv_eq_ci_lit(token, "TIMEOUT") ||
           eval_sv_eq_ci_lit(token, "INACTIVITY_TIMEOUT") ||
           eval_sv_eq_ci_lit(token, "HTTP_USERNAME") ||
           eval_sv_eq_ci_lit(token, "HTTP_PASSWORD") ||
           eval_sv_eq_ci_lit(token, "TLS_CAINFO") ||
           eval_sv_eq_ci_lit(token, "NETRC") ||
           eval_sv_eq_ci_lit(token, "NETRC_FILE") ||
           eval_sv_eq_ci_lit(token, "GIT_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "GIT_TAG") ||
           eval_sv_eq_ci_lit(token, "GIT_REMOTE_NAME") ||
           eval_sv_eq_ci_lit(token, "GIT_REMOTE_UPDATE_STRATEGY") ||
           eval_sv_eq_ci_lit(token, "SVN_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "SVN_REVISION") ||
           eval_sv_eq_ci_lit(token, "SVN_USERNAME") ||
           eval_sv_eq_ci_lit(token, "SVN_PASSWORD") ||
           eval_sv_eq_ci_lit(token, "HG_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "HG_TAG") ||
           eval_sv_eq_ci_lit(token, "CVS_REPOSITORY") ||
           eval_sv_eq_ci_lit(token, "CVS_MODULE") ||
           eval_sv_eq_ci_lit(token, "CVS_TAG");
}

static bool fetchcontent_is_flag_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(token, "SYSTEM") ||
           eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE") ||
           eval_sv_eq_ci_lit(token, "QUIET");
}

static bool fetchcontent_is_multi_value_key(String_View token) {
    return eval_sv_eq_ci_lit(token, "URL") ||
           eval_sv_eq_ci_lit(token, "FIND_PACKAGE_ARGS") ||
           eval_sv_eq_ci_lit(token, "GIT_SUBMODULES") ||
           eval_sv_eq_ci_lit(token, "GIT_CONFIG") ||
           eval_sv_eq_ci_lit(token, "HTTP_HEADER") ||
           eval_sv_eq_ci_lit(token, "DOWNLOAD_COMMAND") ||
           eval_sv_eq_ci_lit(token, "UPDATE_COMMAND") ||
           eval_sv_eq_ci_lit(token, "PATCH_COMMAND");
}

static bool fetchcontent_is_known_option_key(String_View token) {
    return fetchcontent_is_flag_key(token) ||
           fetchcontent_is_bool_value_key(token) ||
           fetchcontent_is_single_value_key(token) ||
           fetchcontent_is_multi_value_key(token);
}

static bool fetchcontent_append_list_to_temp(EvalExecContext *ctx, SV_List *out, const SV_List src) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < arena_arr_len(src); i++) {
        if (!eval_sv_arr_push_temp(ctx, out, src[i])) return false;
    }
    return true;
}

static bool fetchcontent_collect_tail_args_temp(EvalExecContext *ctx,
                                                SV_List args,
                                                size_t start,
                                                SV_List *out) {
    if (!ctx || !out) return false;
    *out = NULL;
    for (size_t i = start; i < arena_arr_len(args); i++) {
        if (!eval_sv_arr_push_temp(ctx, out, args[i])) return false;
    }
    return true;
}

static bool fetchcontent_clone_list_to_event(EvalExecContext *ctx, SV_List *out, const SV_List src) {
    if (!ctx || !out) return false;
    *out = NULL;
    for (size_t i = 0; i < arena_arr_len(src); i++) {
        String_View stable = sv_copy_to_event_arena(ctx, src[i]);
        if (eval_should_stop(ctx)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, *out, stable)) return false;
    }
    return true;
}

static bool fetchcontent_clone_declaration_to_event(EvalExecContext *ctx,
                                                    Eval_FetchContent_Declaration *out_decl,
                                                    const Eval_FetchContent_Declaration *src_decl) {
    if (!ctx || !out_decl || !src_decl) return false;
    memset(out_decl, 0, sizeof(*out_decl));

    *out_decl = *src_decl;
    out_decl->name = sv_copy_to_event_arena(ctx, src_decl->name);
    out_decl->canonical_name = sv_copy_to_event_arena(ctx, src_decl->canonical_name);
    out_decl->source_dir = sv_copy_to_event_arena(ctx, src_decl->source_dir);
    out_decl->binary_dir = sv_copy_to_event_arena(ctx, src_decl->binary_dir);
    out_decl->subbuild_dir = sv_copy_to_event_arena(ctx, src_decl->subbuild_dir);
    out_decl->source_subdir = sv_copy_to_event_arena(ctx, src_decl->source_subdir);
    out_decl->url_hash = sv_copy_to_event_arena(ctx, src_decl->url_hash);
    out_decl->url_md5 = sv_copy_to_event_arena(ctx, src_decl->url_md5);
    out_decl->download_name = sv_copy_to_event_arena(ctx, src_decl->download_name);
    out_decl->http_username = sv_copy_to_event_arena(ctx, src_decl->http_username);
    out_decl->http_password = sv_copy_to_event_arena(ctx, src_decl->http_password);
    out_decl->tls_cainfo = sv_copy_to_event_arena(ctx, src_decl->tls_cainfo);
    out_decl->netrc_mode = sv_copy_to_event_arena(ctx, src_decl->netrc_mode);
    out_decl->netrc_file = sv_copy_to_event_arena(ctx, src_decl->netrc_file);
    out_decl->git_repository = sv_copy_to_event_arena(ctx, src_decl->git_repository);
    out_decl->git_tag = sv_copy_to_event_arena(ctx, src_decl->git_tag);
    out_decl->git_remote_name = sv_copy_to_event_arena(ctx, src_decl->git_remote_name);
    out_decl->git_remote_update_strategy = sv_copy_to_event_arena(ctx, src_decl->git_remote_update_strategy);
    out_decl->svn_repository = sv_copy_to_event_arena(ctx, src_decl->svn_repository);
    out_decl->svn_revision = sv_copy_to_event_arena(ctx, src_decl->svn_revision);
    out_decl->svn_username = sv_copy_to_event_arena(ctx, src_decl->svn_username);
    out_decl->svn_password = sv_copy_to_event_arena(ctx, src_decl->svn_password);
    out_decl->hg_repository = sv_copy_to_event_arena(ctx, src_decl->hg_repository);
    out_decl->hg_tag = sv_copy_to_event_arena(ctx, src_decl->hg_tag);
    out_decl->cvs_repository = sv_copy_to_event_arena(ctx, src_decl->cvs_repository);
    out_decl->cvs_module = sv_copy_to_event_arena(ctx, src_decl->cvs_module);
    out_decl->cvs_tag = sv_copy_to_event_arena(ctx, src_decl->cvs_tag);
    if (eval_should_stop(ctx)) return false;

    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->args, src_decl->args)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->urls, src_decl->urls)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->find_package_args, src_decl->find_package_args)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->download_command, src_decl->download_command)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->http_headers, src_decl->http_headers)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->git_config, src_decl->git_config)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->git_submodules, src_decl->git_submodules)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->update_command, src_decl->update_command)) return false;
    if (!fetchcontent_clone_list_to_event(ctx, &out_decl->patch_command, src_decl->patch_command)) return false;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool fetchcontent_try_find_mode_from_var(EvalExecContext *ctx,
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

static bool fetchcontent_emit_mixed_transport_diag(EvalExecContext *ctx,
                                                   const Node *node,
                                                   String_View dependency_name) {
    if (!ctx || !node) return false;
    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                   node,
                                   eval_origin_from_node(ctx, node),
                                   EV_DIAG_ERROR,
                                   EVAL_DIAG_INVALID_VALUE,
                                   "dispatcher",
                                   nob_sv_from_cstr("FetchContent_Declare() may not mix download transports"),
                                   dependency_name);
    return false;
}

static bool fetchcontent_set_transport(EvalExecContext *ctx,
                                       const Node *node,
                                       String_View dependency_name,
                                       Eval_FetchContent_Declaration *decl,
                                       Eval_FetchContent_Transport transport) {
    if (!ctx || !node || !decl || transport == EVAL_FETCHCONTENT_TRANSPORT_NONE) return false;
    if (decl->transport != EVAL_FETCHCONTENT_TRANSPORT_NONE &&
        decl->transport != transport &&
        decl->transport != EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD) {
        return fetchcontent_emit_mixed_transport_diag(ctx, node, dependency_name);
    }
    if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE ||
        decl->transport == EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD) {
        decl->transport = transport;
    }
    return true;
}

static bool fetchcontent_parse_size_value(String_View value, size_t *out_size) {
    if (!out_size || value.count == 0) return false;
    char buf[64] = {0};
    if (value.count >= sizeof(buf)) return false;
    memcpy(buf, value.data, value.count);
    char *end = NULL;
    unsigned long long raw = strtoull(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out_size = (size_t)raw;
    return true;
}

static bool fetchcontent_is_hex_commitish(String_View value) {
    if (value.count < 7 || value.count > 64) return false;
    for (size_t i = 0; i < value.count; i++) {
        unsigned char c = (unsigned char)value.data[i];
        if (!isxdigit(c)) return false;
    }
    return true;
}

static bool fetchcontent_store_single_value(EvalExecContext *ctx,
                                            const Node *node,
                                            String_View dependency_name,
                                            String_View keyword,
                                            String_View value,
                                            Eval_FetchContent_Declaration *decl,
                                            bool saved_details_defaults) {
    (void)saved_details_defaults;
    if (!ctx || !node || !decl) return false;

    if (eval_sv_eq_ci_lit(keyword, "SOURCE_DIR")) {
        value = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_source_dir(ctx), false);
        decl->source_dir = value;
        decl->has_source_dir = true;
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "BINARY_DIR")) {
        value = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_binary_dir(ctx), false);
        decl->binary_dir = value;
        decl->has_binary_dir = true;
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SUBBUILD_DIR")) {
        value = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_binary_dir(ctx), false);
        decl->subbuild_dir = value;
        decl->has_subbuild_dir = true;
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SOURCE_SUBDIR")) {
        decl->source_subdir = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "URL_HASH")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) return false;
        decl->url_hash = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "URL_MD5")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) return false;
        decl->url_md5 = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "DOWNLOAD_NAME")) {
        decl->download_name = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "TIMEOUT")) {
        if (!fetchcontent_parse_size_value(value, &decl->timeout_sec)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent TIMEOUT requires an integer value"),
                                           value);
            return false;
        }
        decl->has_timeout = true;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "INACTIVITY_TIMEOUT")) {
        if (!fetchcontent_parse_size_value(value, &decl->inactivity_timeout_sec)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent INACTIVITY_TIMEOUT requires an integer value"),
                                           value);
            return false;
        }
        decl->has_inactivity_timeout = true;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "HTTP_USERNAME")) {
        decl->http_username = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "HTTP_PASSWORD")) {
        decl->http_password = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "TLS_CAINFO")) {
        decl->tls_cainfo = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "NETRC")) {
        if (!eval_sv_eq_ci_lit(value, "IGNORED") &&
            !eval_sv_eq_ci_lit(value, "OPTIONAL") &&
            !eval_sv_eq_ci_lit(value, "REQUIRED")) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent NETRC requires IGNORED, OPTIONAL, or REQUIRED"),
                                           value);
            return false;
        }
        decl->has_netrc_mode = true;
        decl->netrc_mode = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "NETRC_FILE")) {
        decl->netrc_file = eval_path_resolve_for_cmake_arg(ctx, value, fetchcontent_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_REPOSITORY")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) return false;
        decl->git_repository = fetchcontent_resolve_local_like_path(ctx, value, fetchcontent_current_source_dir(ctx));
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_TAG")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) return false;
        decl->git_tag = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_REMOTE_NAME")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) return false;
        decl->git_remote_name = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "GIT_REMOTE_UPDATE_STRATEGY")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) return false;
        decl->git_remote_update_strategy = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SVN_REPOSITORY")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_SVN)) return false;
        decl->svn_repository = fetchcontent_resolve_local_like_path(ctx, value, fetchcontent_current_source_dir(ctx));
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SVN_REVISION")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_SVN)) return false;
        decl->svn_revision = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SVN_USERNAME")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_SVN)) return false;
        decl->svn_username = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "SVN_PASSWORD")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_SVN)) return false;
        decl->svn_password = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "HG_REPOSITORY")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_HG)) return false;
        decl->hg_repository = fetchcontent_resolve_local_like_path(ctx, value, fetchcontent_current_source_dir(ctx));
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "HG_TAG")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_HG)) return false;
        decl->hg_tag = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "CVS_REPOSITORY")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_CVS)) return false;
        decl->cvs_repository = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "CVS_MODULE")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_CVS)) return false;
        decl->cvs_module = value;
        return true;
    }
    if (eval_sv_eq_ci_lit(keyword, "CVS_TAG")) {
        if (!fetchcontent_set_transport(ctx, node, dependency_name, decl, EVAL_FETCHCONTENT_TRANSPORT_CVS)) return false;
        decl->cvs_tag = value;
        return true;
    }
    return true;
}

static bool fetchcontent_parse_declaration(EvalExecContext *ctx,
                                           const Node *node,
                                           String_View dependency_name,
                                           const SV_List args,
                                           bool saved_details_defaults,
                                           Eval_FetchContent_Declaration *out_decl) {
    if (!ctx || !node || !out_decl) return false;
    memset(out_decl, 0, sizeof(*out_decl));

    out_decl->name = dependency_name;
    out_decl->canonical_name = fetchcontent_lower_temp(ctx, dependency_name);
    out_decl->download_extract_timestamp = false;
    out_decl->git_submodules_recurse = true;
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

        if (fetchcontent_is_flag_key(token)) {
            if (eval_sv_eq_ci_lit(token, "EXCLUDE_FROM_ALL")) out_decl->exclude_from_all = true;
            else if (eval_sv_eq_ci_lit(token, "SYSTEM")) out_decl->system = true;
            else if (eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE")) out_decl->override_find_package = true;
            else if (eval_sv_eq_ci_lit(token, "QUIET")) out_decl->quiet = true;
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
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) {
                    return false;
                }
                out_decl->download_no_extract = truthy;
            } else if (eval_sv_eq_ci_lit(token, "DOWNLOAD_EXTRACT_TIMESTAMP")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) {
                    return false;
                }
                out_decl->download_extract_timestamp = truthy;
            } else if (eval_sv_eq_ci_lit(token, "DOWNLOAD_NO_PROGRESS")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) {
                    return false;
                }
                out_decl->download_no_progress = truthy;
            } else if (eval_sv_eq_ci_lit(token, "UPDATE_DISCONNECTED")) {
                out_decl->has_update_disconnected = true;
                out_decl->update_disconnected = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_SHALLOW")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) {
                    return false;
                }
                out_decl->git_shallow = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_PROGRESS")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) {
                    return false;
                }
                out_decl->git_progress = truthy;
            } else if (eval_sv_eq_ci_lit(token, "GIT_SUBMODULES_RECURSE")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) {
                    return false;
                }
                out_decl->git_submodules_recurse = truthy;
            } else if (eval_sv_eq_ci_lit(token, "TLS_VERIFY")) {
                out_decl->has_tls_verify = true;
                out_decl->tls_verify = truthy;
            } else if (eval_sv_eq_ci_lit(token, "SVN_TRUST_CERT")) {
                if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_SVN)) {
                    return false;
                }
                out_decl->has_svn_trust_cert = true;
                out_decl->svn_trust_cert = truthy;
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

        if (eval_sv_eq_ci_lit(token, "URL")) {
            if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_URL)) return false;
            bool saw_url = false;
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_known_option_key(args[j])) break;
                String_View url = fetchcontent_resolve_local_like_path(ctx, args[j], fetchcontent_current_source_dir(ctx));
                if (eval_should_stop(ctx)) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->urls, url)) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                i = j;
                saw_url = true;
            }
            if (!saw_url) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               eval_origin_from_node(ctx, node),
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_MISSING_REQUIRED,
                                               "dispatcher",
                                               nob_sv_from_cstr("FetchContent URL transport requires at least one URL"),
                                               dependency_name);
                return false;
            }
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "GIT_SUBMODULES")) {
            if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) {
                return false;
            }
            out_decl->has_git_submodules = true;
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_known_option_key(args[j])) break;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->git_submodules, args[j])) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                i = j;
            }
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "GIT_CONFIG")) {
            if (!fetchcontent_set_transport(ctx, node, dependency_name, out_decl, EVAL_FETCHCONTENT_TRANSPORT_GIT)) {
                return false;
            }
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_known_option_key(args[j])) break;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->git_config, args[j])) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                i = j;
            }
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "HTTP_HEADER")) {
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_known_option_key(args[j])) break;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->http_headers, args[j])) return false;
                if (!eval_sv_arr_push_temp(ctx, &out_decl->args, args[j])) return false;
                i = j;
            }
            continue;
        }

        if (eval_sv_eq_ci_lit(token, "DOWNLOAD_COMMAND") ||
            eval_sv_eq_ci_lit(token, "UPDATE_COMMAND") ||
            eval_sv_eq_ci_lit(token, "PATCH_COMMAND")) {
            SV_List *target = NULL;
            bool *has_command = NULL;
            if (eval_sv_eq_ci_lit(token, "DOWNLOAD_COMMAND")) {
                target = &out_decl->download_command;
                has_command = &out_decl->has_download_command;
                if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE) {
                    out_decl->transport = EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD;
                }
            } else if (eval_sv_eq_ci_lit(token, "UPDATE_COMMAND")) {
                target = &out_decl->update_command;
                has_command = &out_decl->has_update_command;
            } else {
                target = &out_decl->patch_command;
                has_command = &out_decl->has_patch_command;
            }

            *has_command = true;
            for (size_t j = i + 1; j < arena_arr_len(args); j++) {
                if (fetchcontent_is_known_option_key(args[j])) break;
                if (!eval_sv_arr_push_temp(ctx, target, args[j])) return false;
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
        if (arena_arr_len(out_decl->urls) == 0 && !out_decl->has_download_command) {
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
    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_SVN && out_decl->svn_repository.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent SVN transport requires SVN_REPOSITORY"),
                                       dependency_name);
        return false;
    }
    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_HG && out_decl->hg_repository.count == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent HG transport requires HG_REPOSITORY"),
                                       dependency_name);
        return false;
    }
    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_CVS &&
        (out_decl->cvs_repository.count == 0 || out_decl->cvs_module.count == 0)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       eval_origin_from_node(ctx, node),
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent CVS transport requires CVS_REPOSITORY and CVS_MODULE"),
                                       dependency_name);
        return false;
    }
    if (out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_GIT) {
        if (out_decl->git_tag.count == 0) out_decl->git_tag = nob_sv_from_cstr("master");
        if (out_decl->git_remote_name.count == 0) out_decl->git_remote_name = nob_sv_from_cstr("origin");
        if (out_decl->git_remote_update_strategy.count == 0) {
            out_decl->git_remote_update_strategy = nob_sv_from_cstr("CHECKOUT");
        }
        if (out_decl->git_shallow && fetchcontent_is_hex_commitish(out_decl->git_tag)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           eval_origin_from_node(ctx, node),
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_VALUE,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent GIT_SHALLOW may not be used with a commit hash GIT_TAG"),
                                           out_decl->git_tag);
            return false;
        }
    }
    if (out_decl->has_download_command && out_decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE) {
        out_decl->transport = EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD;
    }
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool fetchcontent_build_provider_args(EvalExecContext *ctx,
                                             const Eval_FetchContent_Declaration *decl,
                                             SV_List *out_args) {
    if (!ctx || !decl || !out_args) return false;
    *out_args = NULL;

    bool saw_source_dir = false;
    bool saw_binary_dir = false;
    bool skipping_find_package_args = false;
    if (!eval_sv_arr_push_temp(ctx, out_args, decl->name)) return false;
    for (size_t i = 0; i < arena_arr_len(decl->args); i++) {
        String_View token = decl->args[i];
        if (skipping_find_package_args) continue;
        if (eval_sv_eq_ci_lit(token, "OVERRIDE_FIND_PACKAGE")) continue;
        if (decl->try_find_mode == EVAL_FETCHCONTENT_TRY_FIND_NEVER &&
            eval_sv_eq_ci_lit(token, "FIND_PACKAGE_ARGS")) {
            skipping_find_package_args = true;
            continue;
        }
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

static bool fetchcontent_run_inline_command(EvalExecContext *ctx,
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

static bool fetchcontent_run_add_subdirectory(EvalExecContext *ctx,
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

static bool fetchcontent_run_file_download(EvalExecContext *ctx,
                                           String_View dependency_name,
                                           const Eval_FetchContent_Declaration *decl,
                                           String_View input,
                                           String_View output,
                                           String_View *out_log) {
    if (!ctx || input.count == 0 || output.count == 0) return false;
    SV_List args = NULL;
    String_View status_var = nob_sv_from_cstr("__nobify_fetchcontent_download_status");
    String_View log_var = nob_sv_from_cstr("__nobify_fetchcontent_download_log");

    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("DOWNLOAD"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, input)) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, output)) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("STATUS"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, status_var)) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("LOG"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &args, log_var)) return false;

    if (decl && decl->url_hash.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("EXPECTED_HASH"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, decl->url_hash)) return false;
    }
    if (decl && decl->url_md5.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("EXPECTED_MD5"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, decl->url_md5)) return false;
    }
    if (decl && decl->has_timeout) {
        char *buf = eval_sv_to_cstr_temp(ctx, nob_sv_from_cstr(nob_temp_sprintf("%zu", decl->timeout_sec)));
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("TIMEOUT"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr(buf))) return false;
    }
    if (decl && decl->has_inactivity_timeout) {
        char *buf = eval_sv_to_cstr_temp(ctx, nob_sv_from_cstr(nob_temp_sprintf("%zu", decl->inactivity_timeout_sec)));
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("INACTIVITY_TIMEOUT"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr(buf))) return false;
    }
    if (decl && decl->http_username.count > 0) {
        String_View userpwd = decl->http_username;
        if (decl->http_password.count > 0) {
            String_View parts[3] = {
                decl->http_username,
                nob_sv_from_cstr(":"),
                decl->http_password,
            };
            userpwd = svu_join_no_sep_temp(ctx, parts, 3);
            if (eval_should_stop(ctx)) return false;
        }
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("USERPWD"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, userpwd)) return false;
    }

    bool has_tls_verify = decl && decl->has_tls_verify;
    String_View tls_verify_value = has_tls_verify
                                       ? (decl->tls_verify ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))
                                       : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TLS_VERIFY"));
    if ((has_tls_verify || tls_verify_value.count > 0)) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("TLS_VERIFY"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, tls_verify_value)) return false;
    }

    String_View tls_cainfo = (decl && decl->tls_cainfo.count > 0)
                                 ? decl->tls_cainfo
                                 : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TLS_CAINFO"));
    if (tls_cainfo.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("TLS_CAINFO"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, tls_cainfo)) return false;
    }

    String_View netrc_mode = (decl && decl->has_netrc_mode)
                                 ? decl->netrc_mode
                                 : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_NETRC"));
    if (netrc_mode.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("NETRC"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, netrc_mode)) return false;
    }

    String_View netrc_file = (decl && decl->netrc_file.count > 0)
                                 ? decl->netrc_file
                                 : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_NETRC_FILE"));
    if (netrc_file.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("NETRC_FILE"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &args, netrc_file)) return false;
    }

    if (decl) {
        for (size_t i = 0; i < arena_arr_len(decl->http_headers); i++) {
            if (!eval_sv_arr_push_temp(ctx, &args, nob_sv_from_cstr("HTTPHEADER"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &args, decl->http_headers[i])) return false;
        }
    }

    if (!fetchcontent_run_inline_command(ctx, nob_sv_from_cstr("file"), &args)) return false;

    String_View status = eval_var_get_visible(ctx, status_var);
    if (out_log) *out_log = eval_var_get_visible(ctx, log_var);
    (void)eval_var_unset_current(ctx, status_var);
    (void)eval_var_unset_current(ctx, log_var);

    (void)dependency_name;
    return status.count >= 2 && status.data[0] == '0' && status.data[1] == ';';
}

static bool fetchcontent_run_file_archive_extract(EvalExecContext *ctx,
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

static bool fetchcontent_run_find_package(EvalExecContext *ctx,
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

static String_View fetchcontent_path_basename_temp(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("content");
    size_t start = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
    }
    if (start >= path.count) return nob_sv_from_cstr("content");
    return nob_sv_from_parts(path.data + start, path.count - start);
}

static String_View fetchcontent_path_parent_temp(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return nob_sv_from_cstr("");
    size_t end = path.count;
    while (end > 0 && (path.data[end - 1] == '/' || path.data[end - 1] == '\\')) end--;
    while (end > 0 && path.data[end - 1] != '/' && path.data[end - 1] != '\\') end--;
    if (end == 0) return nob_sv_from_cstr(".");
    while (end > 0 && (path.data[end - 1] == '/' || path.data[end - 1] == '\\')) end--;
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(path.data, end));
}

static bool fetchcontent_git_run(EvalExecContext *ctx,
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

static bool fetchcontent_run_command_capture(EvalExecContext *ctx,
                                             String_View working_directory,
                                             const SV_List argv,
                                             String_View *out_stderr,
                                             int *out_exit_code) {
    return fetchcontent_git_run(ctx, working_directory, argv, out_stderr, out_exit_code);
}

static bool fetchcontent_run_decl_command(EvalExecContext *ctx,
                                          const Node *node,
                                          String_View working_directory,
                                          const SV_List argv,
                                          String_View cause) {
    if (!ctx || !node) return false;
    if (arena_arr_len(argv) == 0) return true;
    if (argv[0].count == 0) return true;

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_run_command_capture(ctx, working_directory, argv, &stderr_text, &exit_code)) return false;
    if (exit_code == 0) return true;
    return fetchcontent_git_emit_failure(ctx,
                                         node,
                                         cause,
                                         stderr_text.count > 0 ? stderr_text : argv[0]);
}

static bool fetchcontent_git_emit_failure(EvalExecContext *ctx,
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

static bool fetchcontent_git_update_submodules(EvalExecContext *ctx,
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

static bool fetchcontent_git_checkout(EvalExecContext *ctx,
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

static bool fetchcontent_git_ref_exists_locally(EvalExecContext *ctx,
                                                String_View source_dir,
                                                String_View git_tag,
                                                bool *out_exists) {
    if (!ctx || !out_exists) return false;
    *out_exists = false;
    if (git_tag.count == 0) return true;

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("rev-parse"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--verify"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--quiet"))) return false;

    String_View ref = svu_concat_suffix_temp(ctx, git_tag, "^{commit}");
    if (eval_should_stop(ctx)) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, ref)) return false;

    String_View stderr_text = nob_sv_from_cstr("");
    int exit_code = 0;
    if (!fetchcontent_run_command_capture(ctx, source_dir, argv, &stderr_text, &exit_code)) return false;
    *out_exists = exit_code == 0;
    return true;
}

static bool fetchcontent_git_fetch_and_update(EvalExecContext *ctx,
                                              const Node *node,
                                              const Eval_FetchContent_Declaration *decl,
                                              String_View source_dir,
                                              bool disconnected) {
    if (!ctx || !node || !decl) return false;
    if (decl->has_update_command) {
        return fetchcontent_run_decl_command(ctx,
                                             node,
                                             source_dir,
                                             decl->update_command,
                                             nob_sv_from_cstr("FetchContent custom update command failed"));
    }
    if (disconnected) {
        if (decl->git_tag.count == 0) return true;
        bool ref_exists = false;
        if (!fetchcontent_git_ref_exists_locally(ctx, source_dir, decl->git_tag, &ref_exists)) return false;
        if (!ref_exists) {
            return fetchcontent_git_emit_failure(ctx,
                                                 node,
                                                 nob_sv_from_cstr("FetchContent Git update disconnected and requested ref is not available locally"),
                                                 decl->git_tag);
        }
        return fetchcontent_git_checkout(ctx, node, source_dir, decl->git_tag);
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("git"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("fetch"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--tags"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->git_remote_name)) return false;

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
        if (eval_sv_eq_ci_lit(decl->git_remote_update_strategy, "REBASE") ||
            eval_sv_eq_ci_lit(decl->git_remote_update_strategy, "REBASE_CHECKOUT")) {
            SV_List rebase_argv = NULL;
            if (!eval_sv_arr_push_temp(ctx, &rebase_argv, nob_sv_from_cstr("git"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &rebase_argv, nob_sv_from_cstr("rebase"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &rebase_argv, decl->git_tag)) return false;
            stderr_text = nob_sv_from_cstr("");
            exit_code = 0;
            if (!fetchcontent_git_run(ctx, source_dir, rebase_argv, &stderr_text, &exit_code)) return false;
            if (exit_code != 0) {
                if (eval_sv_eq_ci_lit(decl->git_remote_update_strategy, "REBASE_CHECKOUT")) {
                    if (!fetchcontent_git_checkout(ctx, node, source_dir, decl->git_tag)) return false;
                } else {
                    return fetchcontent_git_emit_failure(ctx,
                                                         node,
                                                         nob_sv_from_cstr("FetchContent Git rebase update failed"),
                                                         stderr_text.count > 0 ? stderr_text : decl->git_tag);
                }
            }
        } else if (!fetchcontent_git_checkout(ctx, node, source_dir, decl->git_tag)) {
            return false;
        }
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

static bool fetchcontent_is_fully_disconnected(EvalExecContext *ctx) {
    return ctx &&
           eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("FETCHCONTENT_FULLY_DISCONNECTED")));
}

static bool fetchcontent_is_updates_disconnected(EvalExecContext *ctx,
                                                 String_View dependency_name,
                                                 const Eval_FetchContent_Declaration *decl,
                                                 bool respect_saved_variables) {
    if (!ctx) return false;
    if (decl && decl->has_update_disconnected) return decl->update_disconnected;
    if (!respect_saved_variables) return false;
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

static bool fetchcontent_populate_url(EvalExecContext *ctx,
                                      const Node *node,
                                      String_View dependency_name,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    if (!fetchcontent_mkdir_p(ctx, source_dir)) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to create SOURCE_DIR"),
                                             source_dir);
    }

    String_View archive_name = decl->download_name.count > 0
                                   ? decl->download_name
                                   : (arena_arr_len(decl->urls) > 0
                                          ? fetchcontent_path_basename_temp(ctx, decl->urls[0])
                                          : nob_sv_from_cstr("content"));
    String_View archive_path = eval_sv_path_join(eval_temp_arena(ctx), source_dir, archive_name);
    if (eval_should_stop(ctx)) return false;

    if (decl->has_download_command) {
        if (!fetchcontent_run_decl_command(ctx,
                                           node,
                                           fetchcontent_current_binary_dir(ctx),
                                           decl->download_command,
                                           nob_sv_from_cstr("FetchContent custom download command failed"))) {
            return false;
        }
        return true;
    }

    bool downloaded = false;
    String_View last_log = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(decl->urls); i++) {
        if (fetchcontent_run_file_download(ctx, dependency_name, decl, decl->urls[i], archive_path, &last_log)) {
            downloaded = true;
            break;
        }
    }
    if (!downloaded) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent URL download failed"),
                                             last_log.count > 0 ? last_log : source_dir);
    }
    if (decl->download_no_extract) return true;
    return fetchcontent_run_file_archive_extract(ctx,
                                                 archive_path,
                                                 source_dir,
                                                 decl->download_extract_timestamp);
}

static bool fetchcontent_populate_git(EvalExecContext *ctx,
                                      const Node *node,
                                      String_View dependency_name,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir,
                                      bool respect_saved_variables) {
    if (!ctx || !node || !decl) return false;
    bool disconnected = fetchcontent_is_updates_disconnected(ctx, dependency_name, decl, respect_saved_variables);
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
    if (decl->git_remote_name.count > 0 && !eval_sv_eq_ci_lit(decl->git_remote_name, "origin")) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--origin"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->git_remote_name)) return false;
    }
    for (size_t i = 0; i < arena_arr_len(decl->git_config); i++) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--config"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->git_config[i])) return false;
    }
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

static bool fetchcontent_populate_svn(EvalExecContext *ctx,
                                      const Node *node,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    String_View svn_dir = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".svn"));
    if (eval_should_stop(ctx)) return false;

    if (fetchcontent_file_exists(ctx, svn_dir)) {
        if (decl->has_update_command) {
            return fetchcontent_run_decl_command(ctx,
                                                 node,
                                                 source_dir,
                                                 decl->update_command,
                                                 nob_sv_from_cstr("FetchContent custom update command failed"));
        }
        SV_List argv = NULL;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("svn"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("update"))) return false;
        if (decl->svn_revision.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-r"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_revision)) return false;
        }
        if (decl->svn_username.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--username"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_username)) return false;
        }
        if (decl->svn_password.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--password"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_password)) return false;
        }
        if (decl->svn_trust_cert) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--trust-server-cert"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--non-interactive"))) return false;
        }
        return fetchcontent_run_decl_command(ctx, node, source_dir, argv, nob_sv_from_cstr("FetchContent SVN update failed"));
    }

    if (!eval_mkdirs_for_parent(ctx, eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".checkout")))) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to prepare SVN destination"),
                                             source_dir);
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("svn"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("checkout"))) return false;
    if (decl->svn_revision.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-r"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_revision)) return false;
    }
    if (decl->svn_username.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--username"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_username)) return false;
    }
    if (decl->svn_password.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--password"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_password)) return false;
    }
    if (decl->svn_trust_cert) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--trust-server-cert"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("--non-interactive"))) return false;
    }
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->svn_repository)) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, source_dir)) return false;
    return fetchcontent_run_decl_command(ctx, node, nob_sv_from_cstr(""), argv, nob_sv_from_cstr("FetchContent SVN checkout failed"));
}

static bool fetchcontent_populate_hg(EvalExecContext *ctx,
                                     const Node *node,
                                     const Eval_FetchContent_Declaration *decl,
                                     String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    String_View hg_dir = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".hg"));
    if (eval_should_stop(ctx)) return false;

    if (fetchcontent_file_exists(ctx, hg_dir)) {
        if (decl->has_update_command) {
            return fetchcontent_run_decl_command(ctx,
                                                 node,
                                                 source_dir,
                                                 decl->update_command,
                                                 nob_sv_from_cstr("FetchContent custom update command failed"));
        }
        SV_List pull_argv = NULL;
        if (!eval_sv_arr_push_temp(ctx, &pull_argv, nob_sv_from_cstr("hg"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &pull_argv, nob_sv_from_cstr("pull"))) return false;
        if (!fetchcontent_run_decl_command(ctx, node, source_dir, pull_argv, nob_sv_from_cstr("FetchContent HG pull failed"))) {
            return false;
        }

        SV_List update_argv = NULL;
        if (!eval_sv_arr_push_temp(ctx, &update_argv, nob_sv_from_cstr("hg"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &update_argv, nob_sv_from_cstr("update"))) return false;
        if (decl->hg_tag.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &update_argv, nob_sv_from_cstr("-r"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &update_argv, decl->hg_tag)) return false;
        }
        return fetchcontent_run_decl_command(ctx, node, source_dir, update_argv, nob_sv_from_cstr("FetchContent HG update failed"));
    }

    if (!eval_mkdirs_for_parent(ctx, eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".clone")))) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to prepare HG destination"),
                                             source_dir);
    }

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("hg"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("clone"))) return false;
    if (decl->hg_tag.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-u"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->hg_tag)) return false;
    }
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->hg_repository)) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, source_dir)) return false;
    return fetchcontent_run_decl_command(ctx, node, nob_sv_from_cstr(""), argv, nob_sv_from_cstr("FetchContent HG clone failed"));
}

static bool fetchcontent_populate_cvs(EvalExecContext *ctx,
                                      const Node *node,
                                      const Eval_FetchContent_Declaration *decl,
                                      String_View source_dir) {
    if (!ctx || !node || !decl) return false;
    String_View cvs_dir = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CVS"));
    if (eval_should_stop(ctx)) return false;

    if (fetchcontent_file_exists(ctx, cvs_dir)) {
        if (decl->has_update_command) {
            return fetchcontent_run_decl_command(ctx,
                                                 node,
                                                 source_dir,
                                                 decl->update_command,
                                                 nob_sv_from_cstr("FetchContent custom update command failed"));
        }
        SV_List argv = NULL;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("cvs"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("update"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-dP"))) return false;
        if (decl->cvs_tag.count > 0) {
            if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-r"))) return false;
            if (!eval_sv_arr_push_temp(ctx, &argv, decl->cvs_tag)) return false;
        }
        return fetchcontent_run_decl_command(ctx, node, source_dir, argv, nob_sv_from_cstr("FetchContent CVS update failed"));
    }

    if (!eval_mkdirs_for_parent(ctx, eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr(".checkout")))) {
        return fetchcontent_git_emit_failure(ctx,
                                             node,
                                             nob_sv_from_cstr("FetchContent failed to prepare CVS destination"),
                                             source_dir);
    }

    String_View checkout_root = fetchcontent_path_parent_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;

    SV_List argv = NULL;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("cvs"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-d"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->cvs_repository)) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("checkout"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-d"))) return false;
    if (!eval_sv_arr_push_temp(ctx, &argv, fetchcontent_path_basename_temp(ctx, source_dir))) return false;
    if (decl->cvs_tag.count > 0) {
        if (!eval_sv_arr_push_temp(ctx, &argv, nob_sv_from_cstr("-r"))) return false;
        if (!eval_sv_arr_push_temp(ctx, &argv, decl->cvs_tag)) return false;
    }
    if (!eval_sv_arr_push_temp(ctx, &argv, decl->cvs_module)) return false;
    return fetchcontent_run_decl_command(ctx, node, checkout_root, argv, nob_sv_from_cstr("FetchContent CVS checkout failed"));
}

static bool fetchcontent_run_patch_command(EvalExecContext *ctx,
                                           const Node *node,
                                           const Eval_FetchContent_Declaration *decl,
                                           String_View source_dir) {
    if (!ctx || !node || !decl || !decl->has_patch_command) return true;
    return fetchcontent_run_decl_command(ctx,
                                         node,
                                         source_dir,
                                         decl->patch_command,
                                         nob_sv_from_cstr("FetchContent patch command failed"));
}

static bool fetchcontent_write_redirect_stub(EvalExecContext *ctx,
                                             String_View dependency_name,
                                             String_View redirect_path,
                                             bool version_file) {
    if (!ctx || dependency_name.count == 0 || redirect_path.count == 0) return false;
    Nob_String_Builder sb = {0};
    if (version_file) {
        nob_sb_append_cstr(&sb, "set(PACKAGE_VERSION_COMPATIBLE 1)\n");
        nob_sb_append_cstr(&sb, "set(PACKAGE_VERSION_EXACT 1)\n");
    } else {
        String_View lower = fetchcontent_lower_temp(ctx, dependency_name);
        if (eval_should_stop(ctx)) {
            nob_sb_free(sb);
            return false;
        }
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
        nob_sb_appendf(&sb,
                       "include(\"${CMAKE_CURRENT_LIST_DIR}/%.*s-extra.cmake\" OPTIONAL)\n",
                       (int)lower.count,
                       lower.data);
        nob_sb_appendf(&sb,
                       "include(\"${CMAKE_CURRENT_LIST_DIR}/%.*sExtra.cmake\" OPTIONAL)\n",
                       (int)dependency_name.count,
                       dependency_name.data);
    }
    String_View contents = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
    bool ok = eval_write_text_file(ctx, redirect_path, contents, false);
    nob_sb_free(sb);
    return ok;
}

static bool fetchcontent_stage_override_redirects(EvalExecContext *ctx,
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

static bool fetchcontent_finalize_population(EvalExecContext *ctx,
                                             const Eval_FetchContent_Declaration *decl,
                                             bool record_state,
                                             bool publish_populated_var,
                                             bool population_processed,
                                             bool populated,
                                             bool source_dir_known,
                                             String_View source_dir,
                                             bool binary_dir_known,
                                             String_View binary_dir,
                                             bool resolved_by_provider,
                                             bool resolved_by_find_package) {
    if (!ctx || !decl) return false;
    if (record_state &&
        !fetchcontent_upsert_state(ctx,
                                   decl->name,
                                   decl->canonical_name,
                                   population_processed,
                                   populated,
                                   source_dir_known,
                                   source_dir,
                                   binary_dir_known,
                                   binary_dir,
                                   resolved_by_provider,
                                   resolved_by_find_package)) {
        return false;
    }
    if (!fetchcontent_publish_default_vars(ctx,
                                           decl->name,
                                           publish_populated_var,
                                           populated,
                                           source_dir_known ? source_dir : nob_sv_from_cstr(""),
                                           binary_dir_known ? binary_dir : nob_sv_from_cstr(""))) {
        return false;
    }
    if (record_state && populated && decl->override_find_package && !fetchcontent_stage_override_redirects(ctx, decl)) return false;
    return true;
}

static bool fetchcontent_run_transport(EvalExecContext *ctx,
                                       const Node *node,
                                       String_View dependency_name,
                                       const Eval_FetchContent_Declaration *decl,
                                       String_View source_dir,
                                       bool respect_saved_variables) {
    if (!ctx || !node || !decl) return false;
    if (decl->has_download_command) {
        return fetchcontent_populate_url(ctx, node, dependency_name, decl, source_dir);
    }
    switch (decl->transport) {
    case EVAL_FETCHCONTENT_TRANSPORT_URL:
    case EVAL_FETCHCONTENT_TRANSPORT_CUSTOM_DOWNLOAD:
        return fetchcontent_populate_url(ctx, node, dependency_name, decl, source_dir);
    case EVAL_FETCHCONTENT_TRANSPORT_GIT:
        return fetchcontent_populate_git(ctx, node, dependency_name, decl, source_dir, respect_saved_variables);
    case EVAL_FETCHCONTENT_TRANSPORT_SVN:
        return fetchcontent_populate_svn(ctx, node, decl, source_dir);
    case EVAL_FETCHCONTENT_TRANSPORT_HG:
        return fetchcontent_populate_hg(ctx, node, decl, source_dir);
    case EVAL_FETCHCONTENT_TRANSPORT_CVS:
        return fetchcontent_populate_cvs(ctx, node, decl, source_dir);
    case EVAL_FETCHCONTENT_TRANSPORT_NONE:
    default:
        return true;
    }
}

static bool fetchcontent_populate_content(EvalExecContext *ctx,
                                          const Node *node,
                                          String_View dependency_name,
                                          const Eval_FetchContent_Declaration *decl,
                                          bool run_add_subdirectory,
                                          bool record_state,
                                          bool publish_populated_var,
                                          bool respect_saved_variables) {
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
    bool has_source_override = override_source_dir.count > 0;
    if (override_source_dir.count > 0) {
        source_dir = eval_path_resolve_for_cmake_arg(ctx, override_source_dir, fetchcontent_current_source_dir(ctx), false);
        if (eval_should_stop(ctx)) return false;
    }

    if (decl->transport == EVAL_FETCHCONTENT_TRANSPORT_NONE &&
        !decl->has_download_command &&
        !decl->has_source_dir &&
        override_source_dir.count == 0) {
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

    if (respect_saved_variables && fetchcontent_is_fully_disconnected(ctx)) {
        if (!fetchcontent_file_exists(ctx, source_dir)) {
            String_View cmakelists = eval_sv_path_join(eval_temp_arena(ctx), source_dir, nob_sv_from_cstr("CMakeLists.txt"));
            if (eval_should_stop(ctx)) return false;
            if (!fetchcontent_file_exists(ctx, cmakelists)) {
                return fetchcontent_git_emit_failure(ctx,
                                                     node,
                                                     nob_sv_from_cstr("FetchContent is fully disconnected and no pre-populated source tree exists"),
                                                     source_dir);
            }
        }
    } else {
        if (!fetchcontent_run_transport(ctx, node, dependency_name, decl, source_dir, respect_saved_variables)) return false;
    }

    if (!fetchcontent_run_patch_command(ctx, node, decl, source_dir)) return false;
    if (!fetchcontent_finalize_population(ctx,
                                          decl,
                                          record_state,
                                          publish_populated_var,
                                          record_state,
                                          true,
                                          true,
                                          source_dir,
                                          true,
                                          binary_dir,
                                          false,
                                          false)) {
        return false;
    }
    if (!fetchcontent_emit_replay_materialization(ctx,
                                                  eval_origin_from_node(ctx, node),
                                                  dependency_name,
                                                  decl,
                                                  source_dir,
                                                  binary_dir,
                                                  has_source_override,
                                                  false,
                                                  false)) {
        return false;
    }

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

    if (eval_should_stop(ctx)) return false;

    return true;
}

static bool fetchcontent_invoke_dependency_provider(EvalExecContext *ctx,
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
    Eval_Result invoke_result;
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
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool fetchcontent_makeavailable_one(EvalExecContext *ctx,
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

    bool had_verify_header_sets = eval_var_defined_visible(ctx, nob_sv_from_cstr("CMAKE_VERIFY_INTERFACE_HEADER_SETS"));
    String_View verify_header_sets_prev = had_verify_header_sets
                                              ? eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_VERIFY_INTERFACE_HEADER_SETS"))
                                              : nob_sv_from_cstr("");
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_VERIFY_INTERFACE_HEADER_SETS"), nob_sv_from_cstr("0"))) {
        fetchcontent_pop_active(ctx, pushed_active);
        return false;
    }

    bool ok = false;
    bool provider_populated = false;
    if (!already_active &&
        !has_source_override &&
        ctx->semantic_state.package.dependency_provider.command_name.count > 0 &&
        ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial) {
        if (!fetchcontent_invoke_dependency_provider(ctx, node, decl, &provider_populated)) {
            goto cleanup;
        }
    }

    if (provider_populated) {
        Eval_FetchContent_State *provider_state = fetchcontent_find_state(ctx, canonical_name);
        ok = provider_state &&
             fetchcontent_publish_default_vars(ctx,
                                               dependency_name,
                                               true,
                                               true,
                                               provider_state->source_dir,
                                               provider_state->binary_dir);
        if (ok) {
            ok = fetchcontent_emit_replay_marker(ctx, eval_origin_from_node(ctx, node));
        }
        goto cleanup;
    }

    bool found_package = false;
    if (!fetchcontent_run_find_package(ctx, dependency_name, decl, &found_package)) {
        goto cleanup;
    }
    if (found_package) {
        Eval_FetchContent_State *decl_state = fetchcontent_find_state(ctx, canonical_name);
        String_View source_dir = decl_state && decl_state->source_dir_known ? decl_state->source_dir : decl->source_dir;
        String_View binary_dir = decl_state && decl_state->binary_dir_known ? decl_state->binary_dir : decl->binary_dir;
        ok = fetchcontent_finalize_population(ctx,
                                              decl,
                                              true,
                                              true,
                                              false,
                                              false,
                                              true,
                                              source_dir,
                                              true,
                                              binary_dir,
                                              false,
                                              true);
        if (ok) ok = fetchcontent_emit_replay_marker(ctx, eval_origin_from_node(ctx, node));
        goto cleanup;
    }

    ok = fetchcontent_populate_content(ctx, node, dependency_name, decl, true, true, true, true);

cleanup:
    if (had_verify_header_sets) {
        (void)eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_VERIFY_INTERFACE_HEADER_SETS"), verify_header_sets_prev);
    } else {
        (void)eval_var_unset_current(ctx, nob_sv_from_cstr("CMAKE_VERIFY_INTERFACE_HEADER_SETS"));
    }
    fetchcontent_pop_active(ctx, pushed_active);
    return ok;
}

static bool fetchcontent_parse_declare_request(EvalExecContext *ctx,
                                               const Node *node,
                                               Cmake_Event_Origin origin,
                                               SV_List args,
                                               FetchContent_Declare_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_Declare() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_Declare(<name> <content options>...)"));
        return false;
    }

    out_req->dependency_name = args[0];
    out_req->canonical_name = fetchcontent_lower_event(ctx, out_req->dependency_name);
    if (eval_should_stop(ctx)) return false;

    out_req->skip_existing = fetchcontent_find_declaration(ctx, out_req->canonical_name) != NULL;
    if (out_req->skip_existing) return true;

    SV_List decl_args = NULL;
    if (!fetchcontent_collect_tail_args_temp(ctx, args, 1, &decl_args)) return false;
    return fetchcontent_parse_declaration(ctx,
                                          node,
                                          out_req->dependency_name,
                                          decl_args,
                                          true,
                                          &out_req->parsed_decl);
}

static bool fetchcontent_execute_declare_request(EvalExecContext *ctx,
                                                 const FetchContent_Declare_Request *req) {
    if (!ctx || !req) return false;
    if (req->skip_existing) return true;

    Eval_FetchContent_Declaration decl = {0};
    if (!fetchcontent_clone_declaration_to_event(ctx, &decl, &req->parsed_decl)) return false;
    if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.fetchcontent.declarations, decl)) {
        return false;
    }
    return fetchcontent_upsert_state(ctx,
                                     decl.name,
                                     decl.canonical_name,
                                     false,
                                     false,
                                     true,
                                     decl.source_dir,
                                     true,
                                     decl.binary_dir,
                                     false,
                                     false);
}

static bool fetchcontent_parse_makeavailable_request(EvalExecContext *ctx,
                                                     const Node *node,
                                                     Cmake_Event_Origin origin,
                                                     SV_List args,
                                                     FetchContent_MakeAvailable_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) == 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_MakeAvailable() requires at least one dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_MakeAvailable(<name>...)"));
        return false;
    }

    out_req->dependency_names = args;
    return true;
}

static bool fetchcontent_execute_makeavailable_request(EvalExecContext *ctx,
                                                       const Node *node,
                                                       const FetchContent_MakeAvailable_Request *req) {
    if (!ctx || !node || !req) return false;
    for (size_t i = 0; i < arena_arr_len(req->dependency_names); i++) {
        if (!fetchcontent_makeavailable_one(ctx, node, req->dependency_names[i])) return false;
        if (eval_should_stop(ctx)) return false;
    }
    return true;
}

static bool fetchcontent_parse_getproperties_request(EvalExecContext *ctx,
                                                     const Node *node,
                                                     Cmake_Event_Origin origin,
                                                     SV_List args,
                                                     FetchContent_GetProperties_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_GetProperties() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> [SOURCE_DIR <out>] [BINARY_DIR <out>] [POPULATED <out>])"));
        return false;
    }

    out_req->dependency_name = args[0];
    out_req->canonical_name = fetchcontent_lower_temp(ctx, out_req->dependency_name);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "SOURCE_DIR")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(SOURCE_DIR) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> SOURCE_DIR <out>)"));
                return false;
            }
            out_req->outputs.source_dir_var = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "BINARY_DIR")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(BINARY_DIR) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> BINARY_DIR <out>)"));
                return false;
            }
            out_req->outputs.binary_dir_var = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "POPULATED")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_GetProperties(POPULATED) requires an output variable"), nob_sv_from_cstr("Usage: FetchContent_GetProperties(<name> POPULATED <out>)"));
                return false;
            }
            out_req->outputs.populated_var = args[++i];
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_GetProperties() received an unexpected argument"),
                                       args[i]);
        return false;
    }

    return true;
}

static bool fetchcontent_getproperties_has_explicit_outputs(const FetchContent_GetProperties_Request *req) {
    if (!req) return false;
    return req->outputs.source_dir_var.count > 0 ||
           req->outputs.binary_dir_var.count > 0 ||
           req->outputs.populated_var.count > 0;
}

static bool fetchcontent_execute_getproperties_request(EvalExecContext *ctx,
                                                       const FetchContent_GetProperties_Request *req) {
    if (!ctx || !req) return false;

    Eval_FetchContent_State *state = fetchcontent_find_state(ctx, req->canonical_name);
    String_View source_dir = (state && state->source_dir_known) ? state->source_dir : nob_sv_from_cstr("");
    String_View binary_dir = (state && state->binary_dir_known) ? state->binary_dir : nob_sv_from_cstr("");
    bool populated = state ? state->populated : false;

    if (!fetchcontent_getproperties_has_explicit_outputs(req)) {
        return fetchcontent_publish_default_vars(ctx,
                                                 req->dependency_name,
                                                 true,
                                                 populated,
                                                 source_dir,
                                                 binary_dir);
    }

    if (req->outputs.source_dir_var.count > 0 &&
        !eval_var_set_current(ctx, req->outputs.source_dir_var, source_dir)) {
        return false;
    }
    if (req->outputs.binary_dir_var.count > 0 &&
        !eval_var_set_current(ctx, req->outputs.binary_dir_var, binary_dir)) {
        return false;
    }
    if (req->outputs.populated_var.count > 0 &&
        !eval_var_set_current(ctx,
                              req->outputs.populated_var,
                              populated ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return false;
    }

    return true;
}

static bool fetchcontent_parse_populate_request(EvalExecContext *ctx,
                                                const Node *node,
                                                Cmake_Event_Origin origin,
                                                SV_List args,
                                                FetchContent_Populate_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_Populate() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_Populate(<name> [<content options>...])"));
        return false;
    }

    out_req->dependency_name = args[0];
    out_req->canonical_name = fetchcontent_lower_temp(ctx, out_req->dependency_name);
    if (eval_should_stop(ctx)) return false;

    out_req->use_saved_declaration = arena_arr_len(args) == 1;
    if (out_req->use_saved_declaration) return true;

    SV_List decl_args = NULL;
    if (!fetchcontent_collect_tail_args_temp(ctx, args, 1, &decl_args)) return false;
    return fetchcontent_parse_declaration(ctx,
                                          node,
                                          out_req->dependency_name,
                                          decl_args,
                                          false,
                                          &out_req->direct_decl);
}

static bool fetchcontent_execute_populate_request(EvalExecContext *ctx,
                                                  const Node *node,
                                                  Cmake_Event_Origin origin,
                                                  const FetchContent_Populate_Request *req) {
    if (!ctx || !node || !req) return false;

    const Eval_FetchContent_Declaration *decl = NULL;
    if (req->use_saved_declaration) {
        decl = fetchcontent_find_declaration(ctx, req->canonical_name);
        if (!decl) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           origin,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_MISSING_REQUIRED,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Populate() requires a prior FetchContent_Declare() when called without content options"),
                                           req->dependency_name);
            return false;
        }
        Eval_FetchContent_State *state = fetchcontent_find_state(ctx, req->canonical_name);
        if (state && state->population_processed) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                           node,
                                           origin,
                                           EV_DIAG_ERROR,
                                           EVAL_DIAG_INVALID_CONTEXT,
                                           "dispatcher",
                                           nob_sv_from_cstr("FetchContent_Populate() may only be called once per dependency when using saved details"),
                                           req->dependency_name);
            return false;
        }
    } else {
        decl = &req->direct_decl;
    }

    return fetchcontent_populate_content(ctx,
                                         node,
                                         req->dependency_name,
                                         decl,
                                         false,
                                         req->use_saved_declaration,
                                         req->use_saved_declaration,
                                         req->use_saved_declaration);
}

static bool fetchcontent_parse_setpopulated_request(EvalExecContext *ctx,
                                                    const Node *node,
                                                    Cmake_Event_Origin origin,
                                                    SV_List args,
                                                    FetchContent_SetPopulated_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 1) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() requires a dependency name"),
                                       nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> [SOURCE_DIR <dir>] [BINARY_DIR <dir>])"));
        return false;
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
        return false;
    }

    out_req->dependency_name = args[0];
    out_req->canonical_name = fetchcontent_lower_temp(ctx, out_req->dependency_name);
    if (eval_should_stop(ctx)) return false;

    String_View active_name =
        ctx->semantic_state.fetchcontent.active_makeavailable[arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable) - 1];
    if (!eval_sv_key_eq(active_name, out_req->canonical_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_INVALID_CONTEXT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() must match the active dependency provider request"),
                                       out_req->dependency_name);
        return false;
    }

    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "SOURCE_DIR")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_SetPopulated(SOURCE_DIR) requires a directory value"), nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> SOURCE_DIR <dir>)"));
                return false;
            }
            out_req->source_dir = eval_path_resolve_for_cmake_arg(ctx, args[++i], fetchcontent_current_source_dir(ctx), false);
            if (eval_should_stop(ctx)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "BINARY_DIR")) {
            if (i + 1 >= arena_arr_len(args)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("FetchContent_SetPopulated(BINARY_DIR) requires a directory value"), nob_sv_from_cstr("Usage: FetchContent_SetPopulated(<name> BINARY_DIR <dir>)"));
                return false;
            }
            out_req->binary_dir = eval_path_resolve_for_cmake_arg(ctx, args[++i], fetchcontent_current_binary_dir(ctx), false);
            if (eval_should_stop(ctx)) return false;
            continue;
        }
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_UNEXPECTED_ARGUMENT,
                                       "dispatcher",
                                       nob_sv_from_cstr("FetchContent_SetPopulated() received an unexpected argument"),
                                       args[i]);
        return false;
    }

    return true;
}

static bool fetchcontent_execute_setpopulated_request(EvalExecContext *ctx,
                                                      const FetchContent_SetPopulated_Request *req) {
    if (!ctx || !req) return false;

    if (!fetchcontent_upsert_state(ctx,
                                   req->dependency_name,
                                   req->canonical_name,
                                   true,
                                   true,
                                   req->source_dir.count > 0,
                                   req->source_dir,
                                   req->binary_dir.count > 0,
                                   req->binary_dir,
                                   true,
                                   false)) {
        return false;
    }
    return fetchcontent_publish_default_vars(ctx,
                                             req->dependency_name,
                                             true,
                                             true,
                                             req->source_dir,
                                             req->binary_dir);
}

Eval_Result eval_handle_fetchcontent_declare(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    FetchContent_Declare_Request req = {0};
    if (!fetchcontent_parse_declare_request(ctx, node, origin, a, &req)) return eval_result_from_ctx(ctx);
    if (!fetchcontent_execute_declare_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_makeavailable(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    FetchContent_MakeAvailable_Request req = {0};
    if (!fetchcontent_parse_makeavailable_request(ctx, node, origin, a, &req)) return eval_result_from_ctx(ctx);
    if (!fetchcontent_execute_makeavailable_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_getproperties(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    FetchContent_GetProperties_Request req = {0};
    if (!fetchcontent_parse_getproperties_request(ctx, node, origin, a, &req)) return eval_result_from_ctx(ctx);
    if (!fetchcontent_execute_getproperties_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_populate(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    FetchContent_Populate_Request req = {0};
    if (!fetchcontent_parse_populate_request(ctx, node, origin, a, &req)) return eval_result_from_ctx(ctx);
    if (!fetchcontent_execute_populate_request(ctx, node, origin, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_fetchcontent_setpopulated(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!fetchcontent_require_module(ctx, node->as.cmd.name, origin)) return eval_result_from_ctx(ctx);

    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    FetchContent_SetPopulated_Request req = {0};
    if (!fetchcontent_parse_setpopulated_request(ctx, node, origin, a, &req)) return eval_result_from_ctx(ctx);
    if (!fetchcontent_execute_setpopulated_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
