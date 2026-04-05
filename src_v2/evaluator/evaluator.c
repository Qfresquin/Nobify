#include "evaluator.h"
#include "evaluator_internal.h"
#include "eval_dispatcher.h"
#include "eval_exec_core.h"
#include "eval_expr.h"
#include "eval_file_internal.h"
#include "eval_meta.h"
#include "eval_compat.h"
#include "eval_diag_classify.h"
#include "eval_report.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "sv_utils.h"
#include "stb_ds.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static void destroy_sub_arena_cb(void *userdata) {
    Arena *arena = (Arena*)userdata;
    arena_destroy(arena);
}

static void eval_registry_cleanup_cb(void *userdata) {
    EvalRegistry *registry = (EvalRegistry*)userdata;
    eval_registry_destroy(registry);
}

static bool eval_host_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

static String_View eval_default_cmake_command_temp(EvalExecContext *ctx) {
    const char *env_path = getenv("CMK2NOB_TEST_CMAKE_BIN");
    if (env_path && env_path[0] != '\0') return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(env_path));

#if defined(_WIN32)
    {
        char resolved[MAX_PATH] = {0};
        DWORD n = SearchPathA(NULL, "cmake", NULL, (DWORD)sizeof(resolved), resolved, NULL);
        if (n > 0 && n < (DWORD)sizeof(resolved) && eval_host_path_is_executable(resolved)) {
            return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(resolved));
        }
    }
#else
    {
        const char *path_env = getenv("PATH");
        if (path_env && path_env[0] != '\0') {
            size_t temp_mark = nob_temp_save();
            const char *segment = path_env;
            while (segment && segment[0] != '\0') {
                const char *sep = strchr(segment, ':');
                size_t seg_len = sep ? (size_t)(sep - segment) : strlen(segment);
                if (seg_len > 0) {
                    const char *candidate = nob_temp_sprintf("%.*s/cmake", (int)seg_len, segment);
                    if (eval_host_path_is_executable(candidate)) {
                        String_View resolved = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(candidate));
                        nob_temp_rewind(temp_mark);
                        return resolved;
                    }
                }
                if (!sep) break;
                segment = sep + 1;
            }
            nob_temp_rewind(temp_mark);
        }
    }
#endif

    return nob_sv_from_cstr("cmake");
}

// -----------------------------------------------------------------------------
// Arena helpers and origin tracking
// -----------------------------------------------------------------------------

Arena *eval_temp_arena(EvalExecContext *ctx) { return ctx ? ctx->arena : NULL; }
Arena *eval_event_arena(EvalExecContext *ctx) { return ctx ? ctx->event_arena : NULL; }
static Arena *eval_tx_arena(EvalExecContext *ctx) { return ctx ? ctx->transaction_arena : NULL; }

String_View sv_copy_to_temp_arena(EvalExecContext *ctx, String_View sv) {
    String_View out = sv_copy_to_arena(ctx ? ctx->arena : NULL, sv);
    if (ctx && sv.count > 0 && out.count == 0) ctx_oom(ctx);
    return out;
}

String_View sv_copy_to_event_arena(EvalExecContext *ctx, String_View sv) {
    String_View out = sv_copy_to_arena(ctx ? ctx->event_arena : NULL, sv);
    if (ctx && sv.count > 0 && out.count == 0) ctx_oom(ctx);
    return out;
}

static String_View sv_copy_to_tx_arena(EvalExecContext *ctx, String_View sv) {
    String_View out = sv_copy_to_arena(eval_tx_arena(ctx), sv);
    if (ctx && sv.count > 0 && out.count == 0) ctx_oom(ctx);
    return out;
}

static Eval_Canonical_Artifact eval_canonical_artifact_copy_to_event(EvalExecContext *ctx,
                                                                     const Eval_Canonical_Artifact *src) {
    Eval_Canonical_Artifact out = {0};
    if (!ctx || !src) return out;
    out.producer = sv_copy_to_event_arena(ctx, src->producer);
    out.kind = sv_copy_to_event_arena(ctx, src->kind);
    out.status = sv_copy_to_event_arena(ctx, src->status);
    out.base_dir = sv_copy_to_event_arena(ctx, src->base_dir);
    out.primary_path = sv_copy_to_event_arena(ctx, src->primary_path);
    out.aux_paths = sv_copy_to_event_arena(ctx, src->aux_paths);
    return out;
}

static Eval_Ctest_Step_Record eval_ctest_step_record_copy_to_event(EvalExecContext *ctx,
                                                                   const Eval_Ctest_Step_Record *src) {
    Eval_Ctest_Step_Record out = {0};
    if (!ctx || !src) return out;
    out.kind = src->kind;
    out.command_name = sv_copy_to_event_arena(ctx, src->command_name);
    out.submit_part = sv_copy_to_event_arena(ctx, src->submit_part);
    out.status = sv_copy_to_event_arena(ctx, src->status);
    out.model = sv_copy_to_event_arena(ctx, src->model);
    out.track = sv_copy_to_event_arena(ctx, src->track);
    out.source_dir = sv_copy_to_event_arena(ctx, src->source_dir);
    out.build_dir = sv_copy_to_event_arena(ctx, src->build_dir);
    out.testing_dir = sv_copy_to_event_arena(ctx, src->testing_dir);
    out.tag = sv_copy_to_event_arena(ctx, src->tag);
    out.tag_file = sv_copy_to_event_arena(ctx, src->tag_file);
    out.tag_dir = sv_copy_to_event_arena(ctx, src->tag_dir);
    out.manifest = sv_copy_to_event_arena(ctx, src->manifest);
    out.output_junit = sv_copy_to_event_arena(ctx, src->output_junit);
    out.submit_files = sv_copy_to_event_arena(ctx, src->submit_files);
    out.has_primary_artifact = src->has_primary_artifact;
    out.primary_artifact_index = src->primary_artifact_index;
    return out;
}

void eval_canonical_draft_init(Eval_Canonical_Draft *draft) {
    if (draft) memset(draft, 0, sizeof(*draft));
}

bool eval_canonical_draft_add_artifact(EvalExecContext *ctx,
                                       Eval_Canonical_Draft *draft,
                                       const Eval_Canonical_Artifact *artifact,
                                       size_t *out_index) {
    if (!ctx || !draft || !artifact) return false;
    if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), draft->artifacts, *artifact)) return false;
    if (out_index) *out_index = arena_arr_len(draft->artifacts) - 1;
    return true;
}

bool eval_canonical_draft_add_ctest_step(EvalExecContext *ctx,
                                         Eval_Canonical_Draft *draft,
                                         const Eval_Ctest_Step_Record *step) {
    if (!ctx || !draft || !step) return false;
    return EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), draft->ctest_steps, *step);
}

bool eval_canonical_draft_commit(EvalExecContext *ctx, const Eval_Canonical_Draft *draft) {
    if (!ctx || !draft) return false;
    Eval_Canonical_State *state = eval_canonical_slice(ctx);
    if (!state) return false;

    for (size_t i = 0; i < arena_arr_len(draft->artifacts); i++) {
        Eval_Canonical_Artifact copied = eval_canonical_artifact_copy_to_event(ctx, &draft->artifacts[i]);
        if (eval_should_stop(ctx)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, state->artifacts, copied)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(draft->ctest_steps); i++) {
        Eval_Ctest_Step_Record copied = eval_ctest_step_record_copy_to_event(ctx, &draft->ctest_steps[i]);
        if (eval_should_stop(ctx)) return false;
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, state->ctest_steps, copied)) return false;
    }

    return true;
}

const Eval_Canonical_Artifact *eval_canonical_artifact_at(const EvalExecContext *ctx, size_t index) {
    const Eval_Canonical_State *state = eval_canonical_slice_const(ctx);
    if (!state || index >= arena_arr_len(state->artifacts)) return NULL;
    return &state->artifacts[index];
}

const Eval_Ctest_Step_Record *eval_ctest_find_last_step_by_command(const EvalExecContext *ctx,
                                                                   String_View command_name) {
    const Eval_Canonical_State *state = eval_canonical_slice_const(ctx);
    if (!state || command_name.count == 0) return NULL;
    for (size_t i = arena_arr_len(state->ctest_steps); i-- > 0;) {
        if (nob_sv_eq(state->ctest_steps[i].command_name, command_name)) return &state->ctest_steps[i];
    }
    return NULL;
}

const Eval_Ctest_Step_Record *eval_ctest_find_last_step_by_part(const EvalExecContext *ctx,
                                                                String_View submit_part) {
    const Eval_Canonical_State *state = eval_canonical_slice_const(ctx);
    if (!state || submit_part.count == 0) return NULL;
    for (size_t i = arena_arr_len(state->ctest_steps); i-- > 0;) {
        if (nob_sv_eq(state->ctest_steps[i].submit_part, submit_part)) return &state->ctest_steps[i];
    }
    return NULL;
}

String_View eval_ctest_step_submit_files(EvalExecContext *ctx, const Eval_Ctest_Step_Record *step) {
    if (!ctx || !step) return nob_sv_from_cstr("");

    SV_List files = {0};
    if (step->has_primary_artifact) {
        const Eval_Canonical_Artifact *artifact = eval_canonical_artifact_at(ctx, step->primary_artifact_index);
        if (artifact) {
            if (artifact->primary_path.count > 0 && !eval_sv_arr_push_temp(ctx, &files, artifact->primary_path)) {
                return nob_sv_from_cstr("");
            }
            if (artifact->aux_paths.count > 0) {
                SV_List aux = {0};
                if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), artifact->aux_paths, &aux)) {
                    return nob_sv_from_cstr("");
                }
                for (size_t i = 0; i < arena_arr_len(aux); i++) {
                    if (aux[i].count == 0) continue;
                    if (!eval_sv_arr_push_temp(ctx, &files, aux[i])) return nob_sv_from_cstr("");
                }
            }
        }
    }
    if (step->submit_files.count > 0) {
        SV_List extra = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), step->submit_files, &extra)) {
            return nob_sv_from_cstr("");
        }
        for (size_t i = 0; i < arena_arr_len(extra); i++) {
            if (extra[i].count == 0) continue;
            bool seen = false;
            for (size_t j = 0; j < arena_arr_len(files); j++) {
                if (nob_sv_eq(files[j], extra[i])) {
                    seen = true;
                    break;
                }
            }
            if (!seen && !eval_sv_arr_push_temp(ctx, &files, extra[i])) return nob_sv_from_cstr("");
        }
    }
    return eval_sv_join_semi_temp(ctx, files, arena_arr_len(files));
}

Event_Origin eval_origin_from_node(const EvalExecContext *ctx, const Node *node) {
    Event_Origin o = {0};
    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    const char *current_file = exec && exec->current_file ? exec->current_file : (ctx ? ctx->current_file : NULL);
    o.file_path = current_file ? nob_sv_from_cstr(current_file) : nob_sv_from_cstr("<input>");
    if (node) {
        o.line = node->line;
        o.col = node->col;
    }
    return o;
}

static bool eval_exec_counts_as_file_depth(Eval_Exec_Context_Kind kind) {
    return kind == EVAL_EXEC_CTX_ROOT ||
           kind == EVAL_EXEC_CTX_INCLUDE ||
           kind == EVAL_EXEC_CTX_SUBDIRECTORY;
}

static void eval_exec_refresh_cached_state(EvalExecContext *ctx) {
    if (!ctx) return;

    size_t file_depth = 0;
    size_t function_depth = 0;
    for (size_t i = 0; i < arena_arr_len(ctx->exec_contexts); i++) {
        Eval_Exec_Context_Kind kind = ctx->exec_contexts[i].kind;
        if (eval_exec_counts_as_file_depth(kind)) file_depth++;
        if (kind == EVAL_EXEC_CTX_FUNCTION) function_depth++;
    }

    ctx->file_eval_depth = file_depth;
    ctx->function_eval_depth = function_depth;

    const Eval_Exec_Context *current = eval_exec_current_const(ctx);
    if (current) {
        ctx->current_file = current->current_file;
        ctx->return_context = current->return_context;
    } else {
        ctx->return_context = EVAL_RETURN_CTX_TOPLEVEL;
    }
}

Eval_Exec_Context *eval_exec_current(EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->exec_contexts) == 0) return NULL;
    return &ctx->exec_contexts[arena_arr_len(ctx->exec_contexts) - 1];
}

const Eval_Exec_Context *eval_exec_current_const(const EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->exec_contexts) == 0) return NULL;
    return &ctx->exec_contexts[arena_arr_len(ctx->exec_contexts) - 1];
}

bool eval_exec_push(EvalExecContext *ctx, Eval_Exec_Context frame) {
    if (!ctx) return false;
    if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->exec_contexts, frame)) return false;
    eval_exec_refresh_cached_state(ctx);
    return true;
}

void eval_exec_pop(EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->exec_contexts) == 0) return;
    arena_arr_set_len(ctx->exec_contexts, arena_arr_len(ctx->exec_contexts) - 1);
    eval_exec_refresh_cached_state(ctx);
}

static Eval_Exec_Context *eval_exec_find_loop_target(EvalExecContext *ctx) {
    if (!ctx) return NULL;
    for (size_t i = arena_arr_len(ctx->exec_contexts); i-- > 0;) {
        if (ctx->exec_contexts[i].loop_depth > 0) return &ctx->exec_contexts[i];
    }
    return NULL;
}

bool eval_exec_request_break(EvalExecContext *ctx) {
    Eval_Exec_Context *target = eval_exec_find_loop_target(ctx);
    if (!target) return false;
    target->pending_flow = EVAL_FLOW_BREAK;
    return true;
}

bool eval_exec_request_continue(EvalExecContext *ctx) {
    Eval_Exec_Context *target = eval_exec_find_loop_target(ctx);
    if (!target) return false;
    target->pending_flow = EVAL_FLOW_CONTINUE;
    return true;
}

bool eval_exec_request_return(EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->exec_contexts) == 0) return false;
    for (size_t i = arena_arr_len(ctx->exec_contexts); i-- > 0;) {
        ctx->exec_contexts[i].pending_flow = EVAL_FLOW_RETURN;
        if (ctx->exec_contexts[i].kind != EVAL_EXEC_CTX_BLOCK) break;
    }
    return true;
}

Eval_Flow_Signal eval_exec_pending_flow(const EvalExecContext *ctx) {
    const Eval_Exec_Context *current = eval_exec_current_const(ctx);
    return current ? current->pending_flow : EVAL_FLOW_NONE;
}

void eval_exec_clear_pending_flow(EvalExecContext *ctx) {
    Eval_Exec_Context *current = eval_exec_current(ctx);
    if (current) current->pending_flow = EVAL_FLOW_NONE;
}

void eval_exec_clear_all_flow(EvalExecContext *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < arena_arr_len(ctx->exec_contexts); i++) {
        ctx->exec_contexts[i].pending_flow = EVAL_FLOW_NONE;
        ctx->exec_contexts[i].loop_depth = 0;
    }
}

size_t eval_exec_current_loop_depth(const EvalExecContext *ctx) {
    const Eval_Exec_Context *target = eval_exec_find_loop_target((EvalExecContext*)ctx);
    return target ? target->loop_depth : 0;
}

bool eval_exec_has_active_kind(const EvalExecContext *ctx, Eval_Exec_Context_Kind kind) {
    if (!ctx) return false;
    for (size_t i = arena_arr_len(ctx->exec_contexts); i-- > 0;) {
        if (ctx->exec_contexts[i].kind == kind) return true;
    }
    return false;
}

bool eval_exec_is_file_scope(const EvalExecContext *ctx) {
    if (!ctx) return false;
    for (size_t i = arena_arr_len(ctx->exec_contexts); i-- > 0;) {
        Eval_Exec_Context_Kind kind = ctx->exec_contexts[i].kind;
        if (kind == EVAL_EXEC_CTX_FUNCTION ||
            kind == EVAL_EXEC_CTX_MACRO ||
            kind == EVAL_EXEC_CTX_BLOCK) {
            return false;
        }
    }
    return true;
}

bool eval_exec_publish_current_vars(EvalExecContext *ctx) {
    if (!ctx) return false;

    const Eval_Exec_Context *exec = eval_exec_current_const(ctx);
    String_View current_source_dir = exec && exec->source_dir.count > 0 ? exec->source_dir : ctx->source_dir;
    String_View current_binary_dir = exec && exec->binary_dir.count > 0 ? exec->binary_dir : ctx->binary_dir;
    String_View current_list_dir = exec && exec->list_dir.count > 0 ? exec->list_dir : ctx->source_dir;
    String_View current_list_file =
        exec && exec->current_file ? nob_sv_from_cstr(exec->current_file)
                                   : (ctx->current_file ? nob_sv_from_cstr(ctx->current_file) : nob_sv_from_cstr(""));

    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), current_source_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), current_binary_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), current_list_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), current_list_file)) return false;
    return true;
}

bool ctx_oom(EvalExecContext *ctx) {
    if (!ctx) return true;
    ctx->oom = true;
    ctx->stop_requested = true;
    return false;
}

bool eval_continue_on_error(EvalExecContext *ctx) {
    return ctx ? ctx->runtime_state.continue_on_error_snapshot : false;
}

void eval_request_stop(EvalExecContext *ctx) {
    if (ctx) ctx->stop_requested = true;
}

void eval_request_stop_on_error(EvalExecContext *ctx) {
    if (!ctx) return;
    if (eval_continue_on_error(ctx)) return;
    ctx->stop_requested = true;
}

void eval_reset_stop_request(EvalExecContext *ctx) {
    if (!ctx) return;
    ctx->stop_requested = false;
}

bool eval_should_stop(EvalExecContext *ctx) {
    if (!ctx) return true;
    return ctx->oom || ctx->stop_requested;
}

// -----------------------------------------------------------------------------
// Command transactions
// -----------------------------------------------------------------------------

static bool eval_emit_event_direct(EvalExecContext *ctx, Event ev) {
    if (!ctx || !ctx->stream) return false;
    if (ev.h.scope_depth == 0) {
        ev.h.scope_depth = (uint32_t)eval_scope_visible_depth(ctx);
    }
    if (ev.h.policy_depth == 0) {
        ev.h.policy_depth = (uint32_t)eval_policy_visible_depth(ctx);
    }
    if (!event_stream_push(ctx->stream, &ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool eval_tx_event_emits_immediately(Event_Kind kind) {
    return kind == EVENT_COMMAND_BEGIN ||
           kind == EVENT_INCLUDE_BEGIN ||
           kind == EVENT_INCLUDE_END ||
           kind == EVENT_ADD_SUBDIRECTORY_BEGIN ||
           kind == EVENT_ADD_SUBDIRECTORY_END ||
           kind == EVENT_DIRECTORY_ENTER ||
           kind == EVENT_DIRECTORY_LEAVE;
}

static bool eval_tx_snapshot_bytes(EvalExecContext *ctx,
                                   const void *src,
                                   size_t elem_size,
                                   size_t count,
                                   void **out_copy) {
    if (out_copy) *out_copy = NULL;
    if (!ctx || !out_copy) return false;
    if (!src || count == 0 || elem_size == 0) return true;

    void *copy = arena_alloc(eval_tx_arena(ctx), elem_size * count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    memcpy(copy, src, elem_size * count);
    *out_copy = copy;
    return true;
}

static bool eval_tx_snapshot_sv_array(EvalExecContext *ctx,
                                      const String_View *src,
                                      size_t count,
                                      String_View **out_copy) {
    return eval_tx_snapshot_bytes(ctx, src, sizeof(*src), count, (void**)out_copy);
}

static size_t eval_tx_hash_entry_count_var(Eval_Var_Entry *entries) {
    size_t count = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (entries[i].key) count++;
    }
    return count;
}

static size_t eval_tx_hash_entry_count_cache(Eval_Cache_Entry *entries) {
    size_t count = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (entries[i].key) count++;
    }
    return count;
}

static size_t eval_tx_hash_entry_count_env(Eval_Process_Env_Entry *entries) {
    size_t count = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (entries[i].key) count++;
    }
    return count;
}

static bool eval_tx_snapshot_var_table(EvalExecContext *ctx,
                                       Eval_Var_Entry *entries,
                                       Eval_Var_Table_Snapshot *out) {
    if (!out) return false;
    out->entries = NULL;
    out->count = 0;
    if (!ctx || !entries) return true;

    out->count = eval_tx_hash_entry_count_var(entries);
    if (out->count == 0) return true;

    out->entries = arena_alloc(eval_tx_arena(ctx), sizeof(*out->entries) * out->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, out->entries, false);

    size_t at = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (!entries[i].key) continue;
        out->entries[at++] = entries[i];
    }
    return true;
}

static bool eval_tx_snapshot_cache_table(EvalExecContext *ctx,
                                         Eval_Cache_Entry *entries,
                                         Eval_Cache_Table_Snapshot *out) {
    if (!out) return false;
    out->entries = NULL;
    out->count = 0;
    if (!ctx || !entries) return true;

    out->count = eval_tx_hash_entry_count_cache(entries);
    if (out->count == 0) return true;

    out->entries = arena_alloc(eval_tx_arena(ctx), sizeof(*out->entries) * out->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, out->entries, false);

    size_t at = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (!entries[i].key) continue;
        out->entries[at++] = entries[i];
    }
    return true;
}

static bool eval_tx_snapshot_env_table(EvalExecContext *ctx,
                                       Eval_Process_Env_Entry *entries,
                                       Eval_Process_Env_Table_Snapshot *out) {
    if (!out) return false;
    out->entries = NULL;
    out->count = 0;
    if (!ctx || !entries) return true;

    out->count = eval_tx_hash_entry_count_env(entries);
    if (out->count == 0) return true;

    out->entries = arena_alloc(eval_tx_arena(ctx), sizeof(*out->entries) * out->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, out->entries, false);

    size_t at = 0;
    ptrdiff_t n = stbds_shlen(entries);
    for (ptrdiff_t i = 0; i < n; i++) {
        if (!entries[i].key) continue;
        out->entries[at++] = entries[i];
    }
    return true;
}

static bool eval_tx_restore_var_table(Eval_Var_Entry **io_entries,
                                      const Eval_Var_Table_Snapshot *snapshot) {
    if (!io_entries || !snapshot) return false;
    if (*io_entries) {
        stbds_shfree(*io_entries);
        *io_entries = NULL;
    }
    for (size_t i = 0; i < snapshot->count; i++) {
        Eval_Var_Entry *entries = *io_entries;
        stbds_shput(entries, snapshot->entries[i].key, snapshot->entries[i].value);
        *io_entries = entries;
    }
    return true;
}

static bool eval_tx_restore_cache_table(Eval_Cache_Entry **io_entries,
                                        const Eval_Cache_Table_Snapshot *snapshot) {
    if (!io_entries || !snapshot) return false;
    if (*io_entries) {
        stbds_shfree(*io_entries);
        *io_entries = NULL;
    }
    for (size_t i = 0; i < snapshot->count; i++) {
        Eval_Cache_Entry *entries = *io_entries;
        stbds_shput(entries, snapshot->entries[i].key, snapshot->entries[i].value);
        *io_entries = entries;
    }
    return true;
}

static bool eval_tx_restore_env_table(Eval_Process_Env_Entry **io_entries,
                                      const Eval_Process_Env_Table_Snapshot *snapshot) {
    if (!io_entries || !snapshot) return false;
    if (*io_entries) {
        stbds_shfree(*io_entries);
        *io_entries = NULL;
    }
    for (size_t i = 0; i < snapshot->count; i++) {
        Eval_Process_Env_Entry *entries = *io_entries;
        stbds_shput(entries, snapshot->entries[i].key, snapshot->entries[i].value);
        *io_entries = entries;
    }
    return true;
}

static bool eval_tx_snapshot_directory_nodes(EvalExecContext *ctx,
                                             Eval_Directory_Node *nodes,
                                             size_t count,
                                             Eval_Directory_Node_Snapshot **out_copy) {
    if (out_copy) *out_copy = NULL;
    if (!ctx || !out_copy) return false;
    if (!nodes || count == 0) return true;

    Eval_Directory_Node_Snapshot *copy = arena_alloc(eval_tx_arena(ctx), sizeof(*copy) * count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    memset(copy, 0, sizeof(*copy) * count);

    for (size_t i = 0; i < count; i++) {
        copy[i].source_dir = nodes[i].source_dir;
        copy[i].binary_dir = nodes[i].binary_dir;
        copy[i].parent_source_dir = nodes[i].parent_source_dir;
        copy[i].parent_binary_dir = nodes[i].parent_binary_dir;
        copy[i].declared_target_count = arena_arr_len(nodes[i].declared_targets);
        copy[i].declared_test_count = arena_arr_len(nodes[i].declared_tests);
        copy[i].definition_binding_count = arena_arr_len(nodes[i].definition_bindings);
        copy[i].macro_name_count = arena_arr_len(nodes[i].macro_names);
        copy[i].listfile_stack_count = arena_arr_len(nodes[i].listfile_stack);
        if (!eval_tx_snapshot_sv_array(ctx,
                                       nodes[i].declared_targets,
                                       copy[i].declared_target_count,
                                       &copy[i].declared_targets)) {
            return false;
        }
        if (!eval_tx_snapshot_sv_array(ctx,
                                       nodes[i].declared_tests,
                                       copy[i].declared_test_count,
                                       &copy[i].declared_tests)) {
            return false;
        }
        if (!eval_tx_snapshot_bytes(ctx,
                                    nodes[i].definition_bindings,
                                    sizeof(*nodes[i].definition_bindings),
                                    copy[i].definition_binding_count,
                                    (void**)&copy[i].definition_bindings)) {
            return false;
        }
        if (!eval_tx_snapshot_sv_array(ctx,
                                       nodes[i].macro_names,
                                       copy[i].macro_name_count,
                                       &copy[i].macro_names)) {
            return false;
        }
        if (!eval_tx_snapshot_sv_array(ctx,
                                       nodes[i].listfile_stack,
                                       copy[i].listfile_stack_count,
                                       &copy[i].listfile_stack)) {
            return false;
        }
    }

    *out_copy = copy;
    return true;
}

static bool eval_tx_snapshot_deferred_dirs(EvalExecContext *ctx,
                                           Eval_Deferred_Dir_Frame *frames,
                                           size_t count,
                                           Eval_Deferred_Dir_Frame_Snapshot **out_copy) {
    if (out_copy) *out_copy = NULL;
    if (!ctx || !out_copy) return false;
    if (!frames || count == 0) return true;

    Eval_Deferred_Dir_Frame_Snapshot *copy = arena_alloc(eval_tx_arena(ctx), sizeof(*copy) * count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, false);
    memset(copy, 0, sizeof(*copy) * count);

    for (size_t i = 0; i < count; i++) {
        copy[i].source_dir = frames[i].source_dir;
        copy[i].binary_dir = frames[i].binary_dir;
        copy[i].call_count = arena_arr_len(frames[i].calls);
        if (!eval_tx_snapshot_bytes(ctx,
                                    frames[i].calls,
                                    sizeof(*frames[i].calls),
                                    copy[i].call_count,
                                    (void**)&copy[i].calls)) {
            return false;
        }
    }

    *out_copy = copy;
    return true;
}

static bool eval_tx_restore_directory_nodes(EvalExecContext *ctx,
                                            Eval_Directory_Node_List *io_nodes,
                                            const Eval_Directory_Node_Snapshot *snapshot,
                                            size_t count) {
    if (!ctx || !io_nodes) return false;

    arena_arr_set_len(*io_nodes, count);
    if (count > 0 && !*io_nodes) return ctx_oom(ctx);

    for (size_t i = 0; i < count; i++) {
        Eval_Directory_Node *node = &(*io_nodes)[i];
        memset(node, 0, sizeof(*node));

        node->source_dir = snapshot[i].source_dir;
        node->binary_dir = snapshot[i].binary_dir;
        node->parent_source_dir = snapshot[i].parent_source_dir;
        node->parent_binary_dir = snapshot[i].parent_binary_dir;
        for (size_t j = 0; j < snapshot[i].declared_target_count; j++) {
            if (!EVAL_ARR_PUSH(ctx,
                               ctx->event_arena,
                               node->declared_targets,
                               snapshot[i].declared_targets[j])) {
                return false;
            }
        }
        for (size_t j = 0; j < snapshot[i].declared_test_count; j++) {
            if (!EVAL_ARR_PUSH(ctx,
                               ctx->event_arena,
                               node->declared_tests,
                               snapshot[i].declared_tests[j])) {
                return false;
            }
        }
        for (size_t j = 0; j < snapshot[i].definition_binding_count; j++) {
            if (!EVAL_ARR_PUSH(ctx,
                               ctx->event_arena,
                               node->definition_bindings,
                               snapshot[i].definition_bindings[j])) {
                return false;
            }
        }
        for (size_t j = 0; j < snapshot[i].macro_name_count; j++) {
            if (!EVAL_ARR_PUSH(ctx,
                               ctx->event_arena,
                               node->macro_names,
                               snapshot[i].macro_names[j])) {
                return false;
            }
        }
        for (size_t j = 0; j < snapshot[i].listfile_stack_count; j++) {
            if (!EVAL_ARR_PUSH(ctx,
                               ctx->event_arena,
                               node->listfile_stack,
                               snapshot[i].listfile_stack[j])) {
                return false;
            }
        }
    }

    return true;
}

static bool eval_tx_restore_deferred_dirs(EvalExecContext *ctx,
                                          Eval_Deferred_Dir_Frame_Stack *io_frames,
                                          const Eval_Deferred_Dir_Frame_Snapshot *snapshot,
                                          size_t count) {
    if (!ctx || !io_frames) return false;

    arena_arr_set_len(*io_frames, count);
    if (count > 0 && !*io_frames) return ctx_oom(ctx);

    for (size_t i = 0; i < count; i++) {
        Eval_Deferred_Dir_Frame *frame = &(*io_frames)[i];
        memset(frame, 0, sizeof(*frame));

        frame->source_dir = snapshot[i].source_dir;
        frame->binary_dir = snapshot[i].binary_dir;
        for (size_t j = 0; j < snapshot[i].call_count; j++) {
            if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, frame->calls, snapshot[i].calls[j])) {
                return false;
            }
        }
    }

    return true;
}

static bool eval_tx_projection_append(EvalExecContext *ctx, const Eval_Tx_Projection *projection) {
    if (!ctx || !projection) return false;
    Eval_Command_Transaction *tx = ctx->active_transaction;
    if (!tx) return false;
    return EVAL_ARR_PUSH(ctx, eval_tx_arena(ctx), tx->projections, *projection);
}

static bool eval_tx_flush_diag_projection(EvalExecContext *ctx,
                                          const Eval_Tx_Diag_Projection *diag) {
    if (!ctx || !diag) return false;

    Eval_Error_Class cls = eval_diag_error_class(diag->code);
    Diag_Severity logger_input_sev = (diag->input_severity == EV_DIAG_ERROR) ? DIAG_SEV_ERROR : DIAG_SEV_WARNING;

    diag_log(logger_input_sev,
             "evaluator",
             ctx->current_file ? ctx->current_file : "<input>",
             diag->origin.line,
             diag->origin.col,
             nob_temp_sprintf("%.*s", (int)diag->command.count, diag->command.data ? diag->command.data : ""),
             diag->cause.data ? diag->cause.data : "",
             diag->hint.data ? diag->hint.data : "");

    Event ev = {0};
    ev.h.kind = EVENT_DIAG;
    ev.h.origin = diag->origin;
    ev.h.scope_depth = diag->scope_depth;
    ev.h.policy_depth = diag->policy_depth;
    ev.as.diag.severity = diag->effective_severity;
    ev.as.diag.component = diag->component;
    ev.as.diag.command = diag->command;
    ev.as.diag.code = eval_diag_code_to_sv(diag->code);
    ev.as.diag.error_class = eval_error_class_to_sv(cls);
    ev.as.diag.cause = diag->cause;
    ev.as.diag.hint = diag->hint;
    if (!eval_emit_event_direct(ctx, ev)) return false;

    eval_report_record_diag(ctx, diag->input_severity, diag->effective_severity, diag->code);
    return true;
}

static bool eval_tx_projection_keep_on_failure(const Eval_Tx_Projection *projection) {
    if (!projection) return false;
    if (projection->kind == EVAL_TX_PROJECTION_DIAG) return true;
    return projection->as.event.h.kind == EVENT_COMMAND_BEGIN ||
           projection->as.event.h.kind == EVENT_COMMAND_END;
}

size_t eval_pending_warning_count(const EvalExecContext *ctx) {
    if (!ctx) return 0;

    size_t count = ctx->runtime_state.run_report.warning_count;
    for (const Eval_Command_Transaction *tx = ctx->active_transaction; tx; tx = tx->parent) {
        count += tx->pending_warning_count;
    }
    return count;
}

size_t eval_pending_error_count(const EvalExecContext *ctx) {
    if (!ctx) return 0;

    size_t count = ctx->runtime_state.run_report.error_count;
    for (const Eval_Command_Transaction *tx = ctx->active_transaction; tx; tx = tx->parent) {
        count += tx->pending_error_count;
    }
    return count;
}

bool eval_command_tx_push_event(EvalExecContext *ctx, const Event *ev, bool allow_stopped) {
    if (!ctx || !ev) return false;
    if (!allow_stopped && eval_should_stop(ctx)) return false;
    if (allow_stopped && (ctx->oom || !ctx->stream)) return false;

    Event buffered = *ev;
    buffered.h.scope_depth = (uint32_t)eval_scope_visible_depth(ctx);
    buffered.h.policy_depth = (uint32_t)eval_policy_visible_depth(ctx);

    if (!ctx->active_transaction) {
        return eval_emit_event_direct(ctx, buffered);
    }

    if (eval_tx_event_emits_immediately(buffered.h.kind)) {
        return eval_emit_event_direct(ctx, buffered);
    }

    if (!event_copy_into_arena(eval_tx_arena(ctx), &buffered)) {
        return ctx_oom(ctx);
    }

    Eval_Tx_Projection projection = {0};
    projection.kind = EVAL_TX_PROJECTION_EVENT;
    projection.as.event = buffered;
    return eval_tx_projection_append(ctx, &projection);
}

bool eval_command_tx_push_diag(EvalExecContext *ctx,
                               Event_Diag_Severity input_sev,
                               Event_Diag_Severity effective_sev,
                               Eval_Diag_Code code,
                               String_View component,
                               String_View command,
                               Event_Origin origin,
                               String_View cause,
                               String_View hint) {
    if (!ctx) return false;

    if (!ctx->active_transaction) {
        Eval_Tx_Diag_Projection diag = {
            .input_severity = input_sev,
            .effective_severity = effective_sev,
            .code = code,
            .component = component,
            .command = command,
            .origin = origin,
            .cause = cause,
            .hint = hint,
        };
        return eval_tx_flush_diag_projection(ctx, &diag);
    }

    Eval_Tx_Projection projection = {0};
    projection.kind = EVAL_TX_PROJECTION_DIAG;
    projection.as.diag.input_severity = input_sev;
    projection.as.diag.effective_severity = effective_sev;
    projection.as.diag.code = code;
    projection.as.diag.scope_depth = (uint32_t)eval_scope_visible_depth(ctx);
    projection.as.diag.policy_depth = (uint32_t)eval_policy_visible_depth(ctx);
    projection.as.diag.component = sv_copy_to_tx_arena(ctx, component);
    projection.as.diag.command = sv_copy_to_tx_arena(ctx, command);
    projection.as.diag.origin = origin;
    projection.as.diag.cause = sv_copy_to_tx_arena(ctx, cause);
    projection.as.diag.hint = sv_copy_to_tx_arena(ctx, hint);
    if (eval_should_stop(ctx)) return false;
    if (input_sev == EV_DIAG_WARNING) {
        ctx->active_transaction->pending_warning_count++;
    }
    if (effective_sev == EV_DIAG_ERROR) {
        ctx->active_transaction->saw_error_diag = true;
        ctx->active_transaction->pending_error_count++;
    }
    return eval_tx_projection_append(ctx, &projection);
}

bool eval_command_tx_begin(EvalExecContext *ctx, Eval_Command_Transaction *tx) {
    if (!ctx || !tx || !eval_tx_arena(ctx)) return false;
    memset(tx, 0, sizeof(*tx));
    tx->parent = ctx->active_transaction;
    tx->mark = arena_mark(eval_tx_arena(ctx));
    tx->active = true;
    tx->scope_var_count = arena_arr_len(ctx->scope_state.scopes);
    tx->visible_scope_depth = ctx->scope_state.visible_scope_depth;
    tx->dependency_provider = ctx->semantic_state.package.dependency_provider;
    tx->next_deferred_call_id = ctx->file_state.next_deferred_call_id;
    tx->cpack_module_loaded = ctx->cpack_module_loaded;
    tx->cpack_component_module_loaded = ctx->cpack_component_module_loaded;
    tx->fetchcontent_module_loaded = ctx->fetchcontent_module_loaded;

    if (tx->scope_var_count > 0) {
        tx->scope_vars = arena_alloc(eval_tx_arena(ctx), sizeof(*tx->scope_vars) * tx->scope_var_count);
        EVAL_OOM_RETURN_IF_NULL(ctx, tx->scope_vars, false);
        memset(tx->scope_vars, 0, sizeof(*tx->scope_vars) * tx->scope_var_count);
        for (size_t i = 0; i < tx->scope_var_count; i++) {
            if (!eval_tx_snapshot_var_table(ctx, ctx->scope_state.scopes[i].vars, &tx->scope_vars[i])) return false;
        }
    }

    if (!eval_tx_snapshot_cache_table(ctx, ctx->scope_state.cache_entries, &tx->cache_entries)) return false;
    if (!eval_tx_snapshot_env_table(ctx, ctx->process_state.env_overrides, &tx->env_overrides)) return false;

    tx->message_check_count = arena_arr_len(ctx->message_check_stack);
    if (!eval_tx_snapshot_sv_array(ctx, ctx->message_check_stack, tx->message_check_count, &tx->message_check_stack)) return false;

    tx->directory_node_count = arena_arr_len(ctx->semantic_state.directories.nodes);
    if (!eval_tx_snapshot_directory_nodes(ctx,
                                          ctx->semantic_state.directories.nodes,
                                          tx->directory_node_count,
                                          &tx->directory_nodes)) {
        return false;
    }

    tx->property_record_count = arena_arr_len(ctx->semantic_state.properties.records);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.properties.records,
                                sizeof(*ctx->semantic_state.properties.records),
                                tx->property_record_count,
                                (void**)&tx->property_records)) {
        return false;
    }

    tx->target_record_count = arena_arr_len(ctx->semantic_state.targets.records);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.targets.records,
                                sizeof(*ctx->semantic_state.targets.records),
                                tx->target_record_count,
                                (void**)&tx->target_records)) {
        return false;
    }
    tx->target_alias_count = arena_arr_len(ctx->semantic_state.targets.aliases);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.targets.aliases,
                                   tx->target_alias_count,
                                   &tx->target_aliases)) {
        return false;
    }
    tx->test_record_count = arena_arr_len(ctx->semantic_state.tests.records);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.tests.records,
                                sizeof(*ctx->semantic_state.tests.records),
                                tx->test_record_count,
                                (void**)&tx->test_records)) {
        return false;
    }
    tx->install_component_count = arena_arr_len(ctx->semantic_state.install.components);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.install.components,
                                   tx->install_component_count,
                                   &tx->install_components)) {
        return false;
    }
    tx->export_count = arena_arr_len(ctx->semantic_state.export_state.exports);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.export_state.exports,
                                   tx->export_count,
                                   &tx->export_entries)) {
        return false;
    }
    tx->active_find_package_count = arena_arr_len(ctx->semantic_state.package.active_find_packages);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.package.active_find_packages,
                                   tx->active_find_package_count,
                                   &tx->active_find_packages)) {
        return false;
    }
    tx->found_package_count = arena_arr_len(ctx->semantic_state.package.found_packages);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.package.found_packages,
                                   tx->found_package_count,
                                   &tx->found_packages)) {
        return false;
    }
    tx->not_found_package_count = arena_arr_len(ctx->semantic_state.package.not_found_packages);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.package.not_found_packages,
                                   tx->not_found_package_count,
                                   &tx->not_found_packages)) {
        return false;
    }
    tx->package_registry_entry_count = arena_arr_len(ctx->semantic_state.package.registry_entries);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.package.registry_entries,
                                sizeof(*ctx->semantic_state.package.registry_entries),
                                tx->package_registry_entry_count,
                                (void**)&tx->package_registry_entries)) {
        return false;
    }
    tx->file_api_next_reply_nonce = ctx->semantic_state.file_api.next_reply_nonce;
    tx->file_api_query_count = arena_arr_len(ctx->semantic_state.file_api.queries);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.file_api.queries,
                                sizeof(*ctx->semantic_state.file_api.queries),
                                tx->file_api_query_count,
                                (void**)&tx->file_api_queries)) {
        return false;
    }
    tx->fetchcontent_declaration_count = arena_arr_len(ctx->semantic_state.fetchcontent.declarations);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.fetchcontent.declarations,
                                sizeof(*ctx->semantic_state.fetchcontent.declarations),
                                tx->fetchcontent_declaration_count,
                                (void**)&tx->fetchcontent_declarations)) {
        return false;
    }
    tx->fetchcontent_state_count = arena_arr_len(ctx->semantic_state.fetchcontent.states);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->semantic_state.fetchcontent.states,
                                sizeof(*ctx->semantic_state.fetchcontent.states),
                                tx->fetchcontent_state_count,
                                (void**)&tx->fetchcontent_states)) {
        return false;
    }
    tx->active_makeavailable_count = arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->semantic_state.fetchcontent.active_makeavailable,
                                   tx->active_makeavailable_count,
                                   &tx->active_makeavailable)) {
        return false;
    }

    tx->user_command_count = arena_arr_len(ctx->command_state.user_commands);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->command_state.user_commands,
                                sizeof(*ctx->command_state.user_commands),
                                tx->user_command_count,
                                (void**)&tx->user_commands)) {
        return false;
    }
    tx->watched_variable_count = arena_arr_len(ctx->command_state.watched_variables);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->command_state.watched_variables,
                                   tx->watched_variable_count,
                                   &tx->watched_variables)) {
        return false;
    }
    tx->watched_variable_command_count = arena_arr_len(ctx->command_state.watched_variable_commands);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->command_state.watched_variable_commands,
                                   tx->watched_variable_command_count,
                                   &tx->watched_variable_commands)) {
        return false;
    }
    tx->property_definition_count = arena_arr_len(ctx->property_definitions);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->property_definitions,
                                sizeof(*ctx->property_definitions),
                                tx->property_definition_count,
                                (void**)&tx->property_definitions)) {
        return false;
    }

    tx->file_generate_job_count = arena_arr_len(ctx->file_state.file_generate_jobs);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->file_state.file_generate_jobs,
                                sizeof(*ctx->file_state.file_generate_jobs),
                                tx->file_generate_job_count,
                                (void**)&tx->file_generate_jobs)) {
        return false;
    }
    tx->deferred_dir_count = arena_arr_len(ctx->file_state.deferred_dirs);
    if (!eval_tx_snapshot_deferred_dirs(ctx,
                                        ctx->file_state.deferred_dirs,
                                        tx->deferred_dir_count,
                                        &tx->deferred_dirs)) {
        return false;
    }
    tx->generated_deferred_id_count = arena_arr_len(ctx->file_state.generated_deferred_ids);
    if (!eval_tx_snapshot_sv_array(ctx,
                                   ctx->file_state.generated_deferred_ids,
                                   tx->generated_deferred_id_count,
                                   &tx->generated_deferred_ids)) {
        return false;
    }
    tx->canonical_artifact_count = arena_arr_len(ctx->canonical_state.artifacts);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->canonical_state.artifacts,
                                sizeof(*ctx->canonical_state.artifacts),
                                tx->canonical_artifact_count,
                                (void**)&tx->canonical_artifacts)) {
        return false;
    }
    tx->ctest_step_count = arena_arr_len(ctx->canonical_state.ctest_steps);
    if (!eval_tx_snapshot_bytes(ctx,
                                ctx->canonical_state.ctest_steps,
                                sizeof(*ctx->canonical_state.ctest_steps),
                                tx->ctest_step_count,
                                (void**)&tx->ctest_steps)) {
        return false;
    }

    ctx->active_transaction = tx;
    return true;
}

void eval_command_tx_preserve_scope_vars_on_failure(EvalExecContext *ctx) {
    if (!ctx || !ctx->active_transaction) return;
    ctx->active_transaction->preserve_scope_vars_on_failure = true;
}

bool eval_command_tx_finish(EvalExecContext *ctx, Eval_Command_Transaction *tx, bool commit_state) {
    if (!ctx || !tx || ctx->active_transaction != tx) return false;

#define EVAL_TX_RESTORE_ARRAY(arena_expr, dst, snap_items, snap_count)                                \
    do {                                                                                               \
        if ((dst) || (snap_count) > 0) {                                                               \
            arena_arr_set_len((dst), (snap_count));                                                    \
            if ((snap_count) > 0) memcpy((dst), (snap_items), sizeof(*(dst)) * (snap_count));         \
        }                                                                                              \
    } while (0)

    if (!commit_state) {
        if (arena_arr_len(ctx->scope_state.scopes) > tx->scope_var_count) {
            arena_arr_set_len(ctx->scope_state.scopes, tx->scope_var_count);
        }
        ctx->scope_state.visible_scope_depth = tx->visible_scope_depth;
        if (!tx->preserve_scope_vars_on_failure) {
            for (size_t i = 0; i < tx->scope_var_count; i++) {
                if (!eval_tx_restore_var_table(&ctx->scope_state.scopes[i].vars, &tx->scope_vars[i])) {
                    ctx->active_transaction = tx->parent;
                    arena_rewind(eval_tx_arena(ctx), tx->mark);
                    return false;
                }
            }
        }
        if (!eval_tx_restore_cache_table(&ctx->scope_state.cache_entries, &tx->cache_entries)) {
            ctx->active_transaction = tx->parent;
            arena_rewind(eval_tx_arena(ctx), tx->mark);
            return false;
        }
        if (!eval_tx_restore_env_table(&ctx->process_state.env_overrides, &tx->env_overrides)) {
            ctx->active_transaction = tx->parent;
            arena_rewind(eval_tx_arena(ctx), tx->mark);
            return false;
        }

        EVAL_TX_RESTORE_ARRAY(ctx->event_arena, ctx->message_check_stack, tx->message_check_stack, tx->message_check_count);

        if (!eval_tx_restore_directory_nodes(ctx,
                                             &ctx->semantic_state.directories.nodes,
                                             tx->directory_nodes,
                                             tx->directory_node_count)) {
            ctx->active_transaction = tx->parent;
            arena_rewind(eval_tx_arena(ctx), tx->mark);
            return false;
        }

        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.properties.records,
                              tx->property_records,
                              tx->property_record_count);
        EVAL_TX_RESTORE_ARRAY(ctx->semantic_state.targets.arena,
                              ctx->semantic_state.targets.records,
                              tx->target_records,
                              tx->target_record_count);
        EVAL_TX_RESTORE_ARRAY(ctx->semantic_state.targets.arena,
                              ctx->semantic_state.targets.aliases,
                              tx->target_aliases,
                              tx->target_alias_count);
        EVAL_TX_RESTORE_ARRAY(ctx->semantic_state.tests.arena,
                              ctx->semantic_state.tests.records,
                              tx->test_records,
                              tx->test_record_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.install.components,
                              tx->install_components,
                              tx->install_component_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.export_state.exports,
                              tx->export_entries,
                              tx->export_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.package.active_find_packages,
                              tx->active_find_packages,
                              tx->active_find_package_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.package.found_packages,
                              tx->found_packages,
                              tx->found_package_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.package.not_found_packages,
                              tx->not_found_packages,
                              tx->not_found_package_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.package.registry_entries,
                              tx->package_registry_entries,
                              tx->package_registry_entry_count);
        ctx->semantic_state.package.dependency_provider = tx->dependency_provider;
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.file_api.queries,
                              tx->file_api_queries,
                              tx->file_api_query_count);
        ctx->semantic_state.file_api.next_reply_nonce = tx->file_api_next_reply_nonce;
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.fetchcontent.declarations,
                              tx->fetchcontent_declarations,
                              tx->fetchcontent_declaration_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.fetchcontent.states,
                              tx->fetchcontent_states,
                              tx->fetchcontent_state_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->semantic_state.fetchcontent.active_makeavailable,
                              tx->active_makeavailable,
                              tx->active_makeavailable_count);

        EVAL_TX_RESTORE_ARRAY(ctx->command_state.user_commands_arena,
                              ctx->command_state.user_commands,
                              tx->user_commands,
                              tx->user_command_count);
        EVAL_TX_RESTORE_ARRAY(ctx->command_state.user_commands_arena,
                              ctx->command_state.watched_variables,
                              tx->watched_variables,
                              tx->watched_variable_count);
        EVAL_TX_RESTORE_ARRAY(ctx->command_state.user_commands_arena,
                              ctx->command_state.watched_variable_commands,
                              tx->watched_variable_commands,
                              tx->watched_variable_command_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->property_definitions,
                              tx->property_definitions,
                              tx->property_definition_count);

        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->file_state.file_generate_jobs,
                              tx->file_generate_jobs,
                              tx->file_generate_job_count);
        if (!eval_tx_restore_deferred_dirs(ctx,
                                           &ctx->file_state.deferred_dirs,
                                           tx->deferred_dirs,
                                           tx->deferred_dir_count)) {
            ctx->active_transaction = tx->parent;
            arena_rewind(eval_tx_arena(ctx), tx->mark);
            return false;
        }
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->file_state.generated_deferred_ids,
                              tx->generated_deferred_ids,
                              tx->generated_deferred_id_count);
        ctx->file_state.next_deferred_call_id = tx->next_deferred_call_id;
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->canonical_state.artifacts,
                              tx->canonical_artifacts,
                              tx->canonical_artifact_count);
        EVAL_TX_RESTORE_ARRAY(ctx->event_arena,
                              ctx->canonical_state.ctest_steps,
                              tx->ctest_steps,
                              tx->ctest_step_count);
        ctx->cpack_module_loaded = tx->cpack_module_loaded;
        ctx->cpack_component_module_loaded = tx->cpack_component_module_loaded;
        ctx->fetchcontent_module_loaded = tx->fetchcontent_module_loaded;
    }

    bool flush_ok = true;
    for (size_t i = 0; i < arena_arr_len(tx->projections); i++) {
        Eval_Tx_Projection *projection = &tx->projections[i];
        if (!commit_state && !eval_tx_projection_keep_on_failure(projection)) continue;
        if (projection->kind == EVAL_TX_PROJECTION_EVENT) {
            flush_ok = flush_ok && eval_emit_event_direct(ctx, projection->as.event);
        } else {
            flush_ok = flush_ok && eval_tx_flush_diag_projection(ctx, &projection->as.diag);
        }
    }

    ctx->active_transaction = tx->parent;
    arena_rewind(eval_tx_arena(ctx), tx->mark);
    return flush_ok;

#undef EVAL_TX_RESTORE_ARRAY
}

// -----------------------------------------------------------------------------
// Diagnostics (error as data)
// -----------------------------------------------------------------------------

Eval_Result eval_emit_diag(EvalExecContext *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Event_Origin origin,
                           String_View cause,
                           String_View hint) {
    return eval_emit_diag_with_severity(ctx,
                                        eval_diag_default_severity(code),
                                        code,
                                        component,
                                        command,
                                        origin,
                                        cause,
                                        hint);
}

static Diag_Severity eval_diag_to_global_severity(Event_Diag_Severity sev) {
    return sev == EV_DIAG_ERROR ? DIAG_SEV_ERROR : DIAG_SEV_WARNING;
}

static Event_Diag_Severity eval_diag_from_global_severity(Diag_Severity sev) {
    return sev == DIAG_SEV_ERROR ? EV_DIAG_ERROR : EV_DIAG_WARNING;
}

static Event_Diag_Severity eval_diag_effective_input_severity(const EvalExecContext *ctx,
                                                              Event_Diag_Severity sev,
                                                              Eval_Diag_Code code) {
    Event_Diag_Severity out = eval_compat_effective_severity(ctx, sev);
    if (!ctx) return out;
    if (out == EV_DIAG_WARNING &&
        eval_diag_counts_as_unsupported(code) &&
        ctx->runtime_state.unsupported_policy == EVAL_UNSUPPORTED_ERROR) {
        return EV_DIAG_ERROR;
    }
    return out;
}

Eval_Result eval_emit_diag_with_severity(EvalExecContext *ctx,
                                         Event_Diag_Severity sev,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Event_Origin origin,
                                         String_View cause,
                                         String_View hint) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();

    Eval_Error_Class cls = eval_diag_error_class(code);
    Event_Diag_Severity report_input_sev = eval_compat_effective_severity(ctx, sev);
    Event_Diag_Severity effective_input_sev = eval_diag_effective_input_severity(ctx, sev, code);
    Diag_Severity logger_input_sev = eval_diag_to_global_severity(effective_input_sev);
    Event_Diag_Severity effective_sev = eval_diag_from_global_severity(diag_effective_severity(logger_input_sev));
    (void)cls;
    if (!eval_command_tx_push_diag(ctx,
                                   report_input_sev,
                                   effective_sev,
                                   code,
                                   component,
                                   command,
                                   origin,
                                   cause,
                                   hint)) {
        return eval_result_fatal();
    }
    (void)eval_compat_decide_on_diag(ctx, effective_sev);
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (effective_sev == EV_DIAG_ERROR) return eval_result_soft_error();
    return eval_result_ok();
}

// -----------------------------------------------------------------------------
// Variable scopes
// -----------------------------------------------------------------------------
static Eval_Var_Entry *eval_scope_var_find(Eval_Var_Entry *vars, String_View key) {
    if (!vars || !key.data) return NULL;
    return stbds_shgetp_null(vars, nob_temp_sv_to_cstr(key));
}

static Eval_Cache_Entry *eval_cache_var_find(Eval_Cache_Entry *entries, String_View key) {
    if (!entries || !key.data) return NULL;
    return stbds_shgetp_null(entries, nob_temp_sv_to_cstr(key));
}

static char *eval_copy_key_cstr_event(EvalExecContext *ctx, String_View key) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(ctx->event_arena, key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (key.count > 0 && key.data) memcpy(buf, key.data, key.count);
    buf[key.count] = '\0';
    return buf;
}

String_View eval_var_get_visible(EvalExecContext *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return nob_sv_from_cstr("");
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    for (size_t d = eval_scope_visible_depth(ctx); d-- > 0;) {
        Var_Scope *s = &scope->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return b->value;
    }
    Eval_Cache_Entry *ce = eval_cache_var_find(scope->cache_entries, key);
    if (ce) return ce->value.data;
    return nob_sv_from_cstr("");
}

static bool eval_variable_watch_notify(EvalExecContext *ctx,
                                       String_View key,
                                       String_View action,
                                       String_View value) {
    if (!ctx || ctx->runtime_state.in_variable_watch_notification || key.count == 0) return true;
    if (nob_sv_starts_with(key, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_"))) return true;

    Eval_Command_State *commands = eval_command_slice(ctx);
    bool watched = false;
    String_View command = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(commands->watched_variables); i++) {
        if (!eval_sv_key_eq(commands->watched_variables[i], key)) continue;
        watched = true;
        if (i < arena_arr_len(commands->watched_variable_commands)) {
            command = commands->watched_variable_commands[i];
        }
        break;
    }
    if (!watched) return true;

    ctx->runtime_state.in_variable_watch_notification = true;
    bool ok = true;
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VAR"), key);
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_ACTION"), action);
    ok = ok && eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VALUE"), value);
    if (ok && command.count > 0) {
        ok = eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_COMMAND"), command);
    }
    if (ok) {
        Cmake_Event_Origin origin = {0};
        ok = EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("eval_legacy"), nob_sv_from_cstr("variable_watch"), origin, nob_sv_from_cstr("variable_watch() observed a watched variable mutation"), key);
    }
    ctx->runtime_state.in_variable_watch_notification = false;
    return ok;
}

bool eval_var_set_current(EvalExecContext *ctx, String_View key, String_View value) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0 || eval_should_stop(ctx)) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    if (b) {
        b->value = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return false;
        return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("SET"), value);
    }

    char *stable_key = eval_copy_key_cstr_event(ctx, key);
    if (!stable_key) return false;
    String_View stable_value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    Eval_Var_Entry *vars = s->vars;
    stbds_shput(vars, stable_key, stable_value);
    s->vars = vars;
    return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("SET"), value);
}

bool eval_var_unset_current(EvalExecContext *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0 || eval_should_stop(ctx)) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    String_View old_value = eval_var_get_visible(ctx, key);
    if (s->vars) {
        (void)stbds_shdel(s->vars, nob_temp_sv_to_cstr(key));
    }
    return eval_variable_watch_notify(ctx, key, nob_sv_from_cstr("UNSET"), old_value);
}

bool eval_var_defined_visible(EvalExecContext *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    for (size_t d = eval_scope_visible_depth(ctx); d-- > 0;) {
        Var_Scope *s = &scope->scopes[d];
        Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
        if (b) return true;
    }
    if (eval_cache_var_find(scope->cache_entries, key)) return true;
    return false;
}

bool eval_var_defined_current(EvalExecContext *ctx, String_View key) {
    if (!ctx || eval_scope_visible_depth(ctx) == 0) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    Eval_Var_Entry *b = eval_scope_var_find(s->vars, key);
    return b != NULL;
}

bool eval_var_collect_visible_names(EvalExecContext *ctx, SV_List *out_names) {
    if (!ctx || !out_names) return false;
    *out_names = NULL;

    Eval_Scope_State *scope_state = eval_scope_slice(ctx);
    for (size_t depth = 0; depth < eval_scope_visible_depth(ctx); depth++) {
        Var_Scope *scope = &scope_state->scopes[depth];
        ptrdiff_t n = stbds_shlen(scope->vars);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!scope->vars[i].key) continue;
            if (!eval_sv_arr_push_temp(ctx, out_names, nob_sv_from_cstr(scope->vars[i].key))) return false;
        }
    }

    return true;
}

bool eval_cache_defined(EvalExecContext *ctx, String_View key) {
    if (!ctx || key.count == 0) return false;
    return eval_cache_var_find(ctx->scope_state.cache_entries, key) != NULL;
}

bool eval_cache_set(EvalExecContext *ctx,
                    String_View key,
                    String_View value,
                    String_View type,
                    String_View doc) {
    if (!ctx || key.count == 0 || eval_should_stop(ctx)) return false;

    Eval_Scope_State *scope = eval_scope_slice(ctx);
    Eval_Cache_Entry *entry = eval_cache_var_find(scope->cache_entries, key);
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return false;
        entry->value.type = sv_copy_to_event_arena(ctx, type);
        if (eval_should_stop(ctx)) return false;
        entry->value.doc = sv_copy_to_event_arena(ctx, doc);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    char *stable_key = eval_copy_key_cstr_event(ctx, key);
    if (!stable_key) return false;

    Eval_Cache_Entry *entries = scope->cache_entries;
    Eval_Cache_Value cache_value = {
        .data = sv_copy_to_event_arena(ctx, value),
        .type = sv_copy_to_event_arena(ctx, type),
        .doc = sv_copy_to_event_arena(ctx, doc),
    };
    if (eval_should_stop(ctx)) return false;
    stbds_shput(entries, stable_key, cache_value);
    scope->cache_entries = entries;
    return true;
}

bool eval_macro_frame_push(EvalExecContext *ctx) {
    if (!ctx) return false;
    Macro_Frame frame = {0};
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->scope_state.macro_frames, frame);
}

void eval_macro_frame_pop(EvalExecContext *ctx) {
    if (!ctx || arena_arr_len(ctx->scope_state.macro_frames) == 0) return;
    arena_arr_set_len(ctx->scope_state.macro_frames, arena_arr_len(ctx->scope_state.macro_frames) - 1);
}

bool eval_macro_bind_set(EvalExecContext *ctx, String_View key, String_View value) {
    if (!ctx || arena_arr_len(ctx->scope_state.macro_frames) == 0) return false;

    Macro_Frame *top = &ctx->scope_state.macro_frames[arena_arr_len(ctx->scope_state.macro_frames) - 1];
    key = sv_copy_to_event_arena(ctx, key);
    value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = arena_arr_len(top->bindings); i-- > 0;) {
        if (eval_sv_key_eq(top->bindings[i].key, key)) {
            top->bindings[i].value = value;
            return true;
        }
    }

    Var_Binding b = {0};
    b.key = key;
    b.value = value;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, top->bindings, b);
}

bool eval_macro_bind_get(EvalExecContext *ctx, String_View key, String_View *out_value) {
    if (!ctx || !out_value) return false;
    for (size_t fi = arena_arr_len(ctx->scope_state.macro_frames); fi-- > 0;) {
        const Macro_Frame *f = &ctx->scope_state.macro_frames[fi];
        for (size_t i = arena_arr_len(f->bindings); i-- > 0;) {
            if (eval_sv_key_eq(f->bindings[i].key, key)) {
                *out_value = f->bindings[i].value;
                return true;
            }
        }
    }
    return false;
}

bool eval_target_known(EvalExecContext *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->records); i++) {
        if (eval_sv_key_eq(targets->records[i].name, name)) return true;
    }
    return false;
}

static Eval_Target_Record *eval_target_find_record(EvalExecContext *ctx, String_View name) {
    if (!ctx) return NULL;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->records); i++) {
        if (eval_sv_key_eq(targets->records[i].name, name)) return &targets->records[i];
    }
    return NULL;
}

static bool eval_directory_is_same_or_descendant(EvalExecContext *ctx,
                                                 String_View candidate_dir,
                                                 String_View ancestor_dir) {
    if (!ctx || candidate_dir.count == 0 || ancestor_dir.count == 0) return false;

    candidate_dir = eval_sv_path_normalize_temp(ctx, candidate_dir);
    if (eval_should_stop(ctx)) return false;
    ancestor_dir = eval_sv_path_normalize_temp(ctx, ancestor_dir);
    if (eval_should_stop(ctx)) return false;

    while (candidate_dir.count > 0) {
        String_View parent = nob_sv_from_cstr("");
        if (svu_eq_ci_sv(candidate_dir, ancestor_dir)) return true;
        if (!eval_directory_parent(ctx, candidate_dir, &parent)) return false;
        if (eval_should_stop(ctx)) return false;
        if (parent.count == 0 || svu_eq_ci_sv(parent, candidate_dir)) break;
        candidate_dir = parent;
    }

    return false;
}

bool eval_target_visible(EvalExecContext *ctx, String_View name) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!ctx || !record) return false;

    if (!record->imported && !(record->alias && !record->alias_global)) return true;
    if (record->imported && record->imported_global) return true;
    if (record->alias && record->alias_global) return true;

    return eval_directory_is_same_or_descendant(ctx,
                                                eval_current_source_dir_for_paths(ctx),
                                                record->declared_dir);
}

static Eval_Test_Record *eval_test_find_record(EvalExecContext *ctx,
                                               String_View name,
                                               String_View declared_dir) {
    if (!ctx) return NULL;
    if (declared_dir.count == 0) declared_dir = eval_current_source_dir_for_paths(ctx);
    declared_dir = eval_sv_path_normalize_temp(ctx, declared_dir);
    if (eval_should_stop(ctx)) return NULL;

    Eval_Test_Model *tests = &ctx->semantic_state.tests;
    for (size_t i = 0; i < arena_arr_len(tests->records); i++) {
        if (!eval_sv_key_eq(tests->records[i].name, name)) continue;
        if (svu_eq_ci_sv(tests->records[i].declared_dir, declared_dir)) return &tests->records[i];
    }
    return NULL;
}

bool eval_target_register(EvalExecContext *ctx, String_View name) {
    if (!ctx || eval_target_find_record(ctx, name)) return true;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    Eval_Target_Record record = {0};
    record.name = sv_copy_to_arena(targets->arena, name);
    if (name.count > 0 && record.name.count == 0) return ctx_oom(ctx);

    String_View declared_dir = eval_current_source_dir_for_paths(ctx);
    record.declared_dir = sv_copy_to_arena(targets->arena, declared_dir);
    if (declared_dir.count > 0 && record.declared_dir.count == 0) return ctx_oom(ctx);
    record.target_type = EV_TARGET_LIBRARY_UNKNOWN;

    if (!EVAL_ARR_PUSH(ctx, targets->arena, targets->records, record)) return false;
    return eval_directory_note_target(ctx, declared_dir, record.name);
}

bool eval_target_set_type(EvalExecContext *ctx, String_View name, Cmake_Target_Type target_type) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!record) return false;
    record->target_type = target_type;
    return true;
}

bool eval_target_get_type(EvalExecContext *ctx, String_View name, Cmake_Target_Type *out_target_type) {
    if (out_target_type) *out_target_type = EV_TARGET_LIBRARY_UNKNOWN;
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!record) return false;
    if (out_target_type) *out_target_type = record->target_type;
    return true;
}

bool eval_target_set_imported(EvalExecContext *ctx, String_View name, bool imported) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!record) return false;
    record->imported = imported;
    return true;
}

bool eval_target_set_imported_global(EvalExecContext *ctx, String_View name, bool imported_global) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!record) return false;
    record->imported_global = imported_global;
    return true;
}

bool eval_target_declared_dir(EvalExecContext *ctx, String_View name, String_View *out_dir) {
    if (!out_dir) return false;
    *out_dir = nob_sv_from_cstr("");
    if (!ctx) return false;

    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->records); i++) {
        if (!eval_sv_key_eq(targets->records[i].name, name)) continue;
        *out_dir = targets->records[i].declared_dir;
        return true;
    }
    return false;
}

bool eval_target_is_imported(EvalExecContext *ctx, String_View name) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    return record ? record->imported : false;
}

bool eval_target_is_imported_global(EvalExecContext *ctx, String_View name) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    return record ? record->imported_global : false;
}

bool eval_target_alias_known(EvalExecContext *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    for (size_t i = 0; i < arena_arr_len(targets->aliases); i++) {
        if (eval_sv_key_eq(targets->aliases[i], name)) return true;
    }
    return false;
}

bool eval_target_alias_register(EvalExecContext *ctx, String_View name) {
    if (!ctx || eval_target_alias_known(ctx, name)) return true;
    Eval_Target_Model *targets = &ctx->semantic_state.targets;
    name = sv_copy_to_event_arena(ctx, name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, targets->arena, targets->aliases, name);
}

bool eval_target_set_alias(EvalExecContext *ctx, String_View alias_name, String_View real_target) {
    if (!ctx) return false;
    Eval_Target_Record *alias_record = eval_target_find_record(ctx, alias_name);
    Eval_Target_Record *real_record = eval_target_find_record(ctx, real_target);
    if (!alias_record || !real_record) return false;

    alias_record->alias = true;
    alias_record->alias_global = real_record->imported ? real_record->imported_global : true;
    alias_record->target_type = real_record->target_type;
    alias_record->alias_of = sv_copy_to_arena(ctx->semantic_state.targets.arena, real_target);
    if (real_target.count > 0 && alias_record->alias_of.count == 0) return ctx_oom(ctx);
    return true;
}

bool eval_target_alias_of(EvalExecContext *ctx, String_View name, String_View *out_real_target) {
    if (out_real_target) *out_real_target = nob_sv_from_cstr("");
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    if (!record) return false;
    if (out_real_target) *out_real_target = record->alias_of;
    return true;
}

bool eval_target_alias_is_global(EvalExecContext *ctx, String_View name) {
    Eval_Target_Record *record = eval_target_find_record(ctx, name);
    return record ? (record->alias && record->alias_global) : false;
}

bool eval_test_known(EvalExecContext *ctx, String_View name) {
    if (!ctx) return false;
    Eval_Test_Model *tests = &ctx->semantic_state.tests;
    for (size_t i = 0; i < arena_arr_len(tests->records); i++) {
        if (eval_sv_key_eq(tests->records[i].name, name)) return true;
    }
    return false;
}

bool eval_test_known_in_directory(EvalExecContext *ctx, String_View name, String_View declared_dir) {
    if (!ctx) return false;
    if (declared_dir.count == 0) declared_dir = eval_current_source_dir_for_paths(ctx);
    declared_dir = eval_sv_path_normalize_temp(ctx, declared_dir);
    if (eval_should_stop(ctx)) return false;

    Eval_Test_Model *tests = &ctx->semantic_state.tests;
    for (size_t i = 0; i < arena_arr_len(tests->records); i++) {
        if (!eval_sv_key_eq(tests->records[i].name, name)) continue;
        if (svu_eq_ci_sv(tests->records[i].declared_dir, declared_dir)) return true;
    }
    return false;
}

bool eval_test_register(EvalExecContext *ctx, String_View name, String_View declared_dir) {
    if (!ctx) return false;
    if (declared_dir.count == 0) declared_dir = eval_current_source_dir_for_paths(ctx);
    declared_dir = eval_sv_path_normalize_temp(ctx, declared_dir);
    if (eval_should_stop(ctx)) return false;
    if (eval_test_known_in_directory(ctx, name, declared_dir)) return true;

    Eval_Test_Model *tests = &ctx->semantic_state.tests;
    Eval_Test_Record record = {0};
    record.name = sv_copy_to_arena(tests->arena, name);
    if (name.count > 0 && record.name.count == 0) return ctx_oom(ctx);
    record.declared_dir = sv_copy_to_arena(tests->arena, declared_dir);
    if (declared_dir.count > 0 && record.declared_dir.count == 0) return ctx_oom(ctx);
    if (!EVAL_ARR_PUSH(ctx, tests->arena, tests->records, record)) return false;
    return eval_directory_note_test(ctx, declared_dir, record.name);
}

bool eval_test_set_working_directory(EvalExecContext *ctx,
                                     String_View name,
                                     String_View declared_dir,
                                     String_View working_directory) {
    Eval_Test_Record *record = eval_test_find_record(ctx, name, declared_dir);
    if (!record) return false;
    record->working_directory = sv_copy_to_arena(ctx->semantic_state.tests.arena, working_directory);
    if (working_directory.count > 0 && record->working_directory.count == 0) return ctx_oom(ctx);
    return true;
}

bool eval_test_working_directory(EvalExecContext *ctx,
                                 String_View name,
                                 String_View declared_dir,
                                 String_View *out_working_directory) {
    if (out_working_directory) *out_working_directory = nob_sv_from_cstr("");
    Eval_Test_Record *record = eval_test_find_record(ctx, name, declared_dir);
    if (!record) return false;
    if (out_working_directory) *out_working_directory = record->working_directory;
    return true;
}

bool eval_install_component_known(EvalExecContext *ctx, String_View name) {
    if (!ctx || name.count == 0) return false;
    Eval_Install_Model *install = &ctx->semantic_state.install;
    for (size_t i = 0; i < arena_arr_len(install->components); i++) {
        if (eval_sv_key_eq(install->components[i], name)) return true;
    }
    return false;
}

bool eval_install_component_register(EvalExecContext *ctx, String_View name) {
    if (!ctx || name.count == 0 || eval_install_component_known(ctx, name)) return true;
    Eval_Install_Model *install = &ctx->semantic_state.install;
    name = sv_copy_to_event_arena(ctx, name);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, install->components, name);
}

// -----------------------------------------------------------------------------
// Argument resolution (token flattening + variable expansion + list splitting)
// -----------------------------------------------------------------------------

static char *sv_ascii_upper_copy(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return NULL;
    char *buf = (char*)arena_alloc(arena, sv.count + 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < sv.count; i++) {
        buf[i] = (char)toupper((unsigned char)sv.data[i]);
    }
    buf[sv.count] = '\0';
    return buf;
}

static char *sv_ascii_upper_heap(String_View sv) {
    if (!sv.data || sv.count == 0) return NULL;
    char *buf = (char*)malloc(sv.count + 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < sv.count; i++) {
        buf[i] = (char)toupper((unsigned char)sv.data[i]);
    }
    buf[sv.count] = '\0';
    return buf;
}

static bool sv_list_push(Arena *arena, SV_List *list, String_View sv) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, sv);
}

static bool sv_strip_cmake_bracket_arg(String_View in, String_View *out) {
    if (!out) return false;
    *out = in;
    if (in.count < 4 || !in.data) return false;
    if (in.data[0] != '[') return false;

    size_t eq_count = 0;
    size_t i = 1;
    while (i < in.count && in.data[i] == '=') {
        eq_count++;
        i++;
    }
    if (i >= in.count || in.data[i] != '[') return false;
    size_t open_len = i + 1; // '[' + '='* + '['

    if (in.count < open_len + 2 + eq_count) return false;
    size_t close_pos = in.count - (eq_count + 2); // start of closing ']'
    if (in.data[close_pos] != ']') return false;
    for (size_t k = 0; k < eq_count; k++) {
        if (in.data[close_pos + 1 + k] != '=') return false;
    }
    if (in.data[in.count - 1] != ']') return false;

    if (close_pos < open_len) return false;
    *out = nob_sv_from_parts(in.data + open_len, close_pos - open_len);
    return true;
}

static String_View arg_to_sv_flat(EvalExecContext *ctx, const Arg *arg) {
    if (!ctx || !arg || arena_arr_len(arg->items) == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) total += arg->items[i].text.count;

    // Flatten into temp arena; caller rewinds at statement boundary.
    char *buf = (char*)arena_alloc(ctx->arena, total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(arg->items); i++) {
        String_View t = arg->items[i].text;
        if (t.count) {
            memcpy(buf + off, t.data, t.count);
            off += t.count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

#define EVAL_LITERAL_BS_BEFORE_DOLLAR_SENTINEL '\x1e'

static String_View arg_restore_literal_bs_sentinel_temp(EvalExecContext *ctx, String_View in) {
    if (!ctx || !in.data || in.count == 0) return in;

    bool has_sentinel = false;
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] == EVAL_LITERAL_BS_BEFORE_DOLLAR_SENTINEL) {
            has_sentinel = true;
            break;
        }
    }
    if (!has_sentinel) return in;

    char *buf = (char*)arena_alloc(ctx->arena, in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < in.count; i++) {
        buf[i] = (in.data[i] == EVAL_LITERAL_BS_BEFORE_DOLLAR_SENTINEL) ? '\\' : in.data[i];
    }
    buf[in.count] = '\0';
    return nob_sv_from_parts(buf, in.count);
}

static String_View arg_process_quoted_literal_temp(EvalExecContext *ctx, String_View in) {
    bool has_escape = false;
    size_t out_count = 0;
    char *buf = NULL;

    if (!ctx) return nob_sv_from_cstr("");
    if (in.count >= 2 && in.data[0] == '"' && in.data[in.count - 1] == '"') {
        in.data += 1;
        in.count -= 2;
    }
    if (in.count == 0) return in;
    for (size_t i = 0; i < in.count; i++) {
        if (in.data[i] == '\\') {
            has_escape = true;
            break;
        }
    }
    if (!has_escape) return in;

    buf = (char*)arena_alloc(ctx->arena, in.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    for (size_t i = 0; i < in.count; i++) {
        char ch = in.data[i];
        if (ch != '\\') {
            buf[out_count++] = ch;
            continue;
        }

        if (i + 1 >= in.count) {
            buf[out_count++] = ch;
            continue;
        }

        size_t slash_begin = i;
        while (i < in.count && in.data[i] == '\\') i++;
        if (i < in.count && in.data[i] == '$') {
            size_t slash_count = i - slash_begin;
            size_t literal_pairs = slash_count / 2u;
            for (size_t k = 0; k < literal_pairs; k++) {
                buf[out_count++] = EVAL_LITERAL_BS_BEFORE_DOLLAR_SENTINEL;
            }
            if ((slash_count % 2u) != 0u) {
                buf[out_count++] = '\\';
                buf[out_count++] = '$';
                continue;
            }
            i--;
            continue;
        }
        i = slash_begin;
        ch = in.data[++i];
        switch (ch) {
            case 't': buf[out_count++] = '\t'; break;
            case 'r': buf[out_count++] = '\r'; break;
            case 'n': buf[out_count++] = '\n'; break;
            case ';': buf[out_count++] = ';'; break;
            case '"': buf[out_count++] = '"'; break;
            case '\\': buf[out_count++] = '\\'; break;
            case '$':
                buf[out_count++] = '\\';
                buf[out_count++] = '$';
                break;
            case '\n': break;
            case '\r':
                if (i + 1 < in.count && in.data[i + 1] == '\n') i++;
                break;
            default:
                buf[out_count++] = '\\';
                buf[out_count++] = ch;
                break;
        }
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

String_View eval_resolve_quoted_arg_temp(EvalExecContext *ctx, String_View flat, bool expand_vars) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View value = arg_process_quoted_literal_temp(ctx, flat);
    if (expand_vars) value = eval_expand_vars(ctx, value);
    value = arg_restore_literal_bs_sentinel_temp(ctx, value);
    return value;
}

static SV_List eval_resolve_args_impl(EvalExecContext *ctx,
                                      const Args *raw_args,
                                      bool expand_vars,
                                      bool split_unquoted_lists) {
    SV_List out = {0};
    if (!ctx || !raw_args || eval_should_stop(ctx)) return out;

    for (size_t i = 0; i < arena_arr_len(*raw_args); i++) {
        const Arg *arg = &(*raw_args)[i];

        String_View flat = arg_to_sv_flat(ctx, arg);

        if (arg->kind == ARG_QUOTED) {
            // Quoted args preserve semicolons as plain text and must unescape
            // source-level escapes before variable expansion, without touching
            // backslashes that come from the expanded variable values.
            String_View expanded = eval_resolve_quoted_arg_temp(ctx, flat, expand_vars);
            if (eval_should_stop(ctx)) return (SV_List){0};
            if (!sv_list_push(ctx->arena, &out, expanded)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        } else if (arg->kind == ARG_BRACKET) {
            String_View expanded = expand_vars ? eval_expand_vars(ctx, flat) : flat;
            if (eval_should_stop(ctx)) return (SV_List){0};
            // Bracket args also preserve semicolons and skip normal splitting.
            String_View stripped = expanded;
            (void)sv_strip_cmake_bracket_arg(expanded, &stripped);
            expanded = stripped;
            if (!sv_list_push(ctx->arena, &out, expanded)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        } else {
            String_View expanded = expand_vars ? eval_expand_vars(ctx, flat) : flat;
            if (eval_should_stop(ctx)) return (SV_List){0};
            // Macro call-site semantics keep unquoted args literal (no list split).
            if (!split_unquoted_lists) {
                if (!sv_list_push(ctx->arena, &out, expanded)) {
                    ctx_oom(ctx);
                    return (SV_List){0};
                }
                continue;
            }

            // Default command/function semantics: unquoted args are list-split by ';'.
            if (expanded.count == 0) continue;
            if (!eval_sv_split_semicolon_genex_aware(ctx->arena, expanded, &out)) {
                ctx_oom(ctx);
                return (SV_List){0};
            }
        }
    }

    return out;
}

SV_List eval_resolve_args(EvalExecContext *ctx, const Args *raw_args) {
    return eval_resolve_args_impl(ctx, raw_args, true, true);
}

SV_List eval_resolve_args_literal(EvalExecContext *ctx, const Args *raw_args) {
    return eval_resolve_args_impl(ctx, raw_args, false, false);
}

// -----------------------------------------------------------------------------
// Native command registration/lookup
// -----------------------------------------------------------------------------

static bool eval_registry_index_rebuild(EvalRegistry *registry) {
    if (!registry) return false;

    if (registry->native_command_index) {
        stbds_shfree(registry->native_command_index);
        registry->native_command_index = NULL;
    }

    size_t count = arena_arr_len(registry->native_commands);
    for (size_t i = 0; i < count; i++) {
        Eval_Native_Command *cmd = &registry->native_commands[i];
        if (!cmd->normalized_name) {
            cmd->normalized_name = sv_ascii_upper_copy(registry->arena, cmd->name);
            if (!cmd->normalized_name) return false;
        }
        stbds_shput(registry->native_command_index, cmd->normalized_name, i);
    }

    return true;
}

const Eval_Native_Command *eval_registry_find_const(const EvalRegistry *registry, String_View name) {
    if (!registry || !name.data || name.count == 0) return NULL;
    if (!registry->native_command_index) return NULL;

    char *lookup_key = sv_ascii_upper_heap(name);
    if (!lookup_key) return NULL;

    Eval_Native_Command_Index_Entry *index = registry->native_command_index;
    Eval_Native_Command_Index_Entry *entry = stbds_shgetp_null(index, lookup_key);
    if (!entry) {
        free(lookup_key);
        return NULL;
    }
    if (entry->value >= arena_arr_len(registry->native_commands)) {
        free(lookup_key);
        return NULL;
    }

    const Eval_Native_Command *out = &registry->native_commands[entry->value];
    free(lookup_key);
    return out;
}

static Eval_Native_Command *eval_registry_find(EvalRegistry *registry, String_View name) {
    return (Eval_Native_Command*)eval_registry_find_const(registry, name);
}

bool eval_registry_register_internal(EvalRegistry *registry,
                                     const EvalNativeCommandDef *def,
                                     bool is_builtin) {
    if (!registry || !registry->arena || !def || !def->handler || def->name.count == 0 || !def->name.data) {
        return false;
    }
    if (registry->mutation_blocked) return false;
    if (eval_registry_find(registry, def->name)) return false;

    Eval_Native_Command cmd = {0};
    cmd.name = sv_copy_to_arena(registry->arena, def->name);
    if (def->name.count > 0 && cmd.name.count == 0) return false;
    cmd.normalized_name = sv_ascii_upper_copy(registry->arena, def->name);
    if (!cmd.normalized_name) return false;
    cmd.handler = def->handler;
    cmd.implemented_level = def->implemented_level;
    cmd.fallback_behavior = def->fallback_behavior;
    cmd.is_builtin = is_builtin;

    if (!arena_arr_push(registry->arena, registry->native_commands, cmd)) return false;
    return eval_registry_index_rebuild(registry);
}

bool eval_registry_unregister_internal(EvalRegistry *registry,
                                       String_View name,
                                       bool allow_builtin_remove) {
    if (!registry || name.count == 0 || !name.data) return false;
    if (registry->mutation_blocked) return false;
    if (!registry->native_command_index) return false;

    char *lookup_key = sv_ascii_upper_heap(name);
    if (!lookup_key) return false;

    Eval_Native_Command_Index_Entry *entry = stbds_shgetp_null(registry->native_command_index, lookup_key);
    if (!entry) {
        free(lookup_key);
        return false;
    }

    size_t idx = entry->value;
    size_t count = arena_arr_len(registry->native_commands);
    if (idx >= count) {
        free(lookup_key);
        return false;
    }
    if (registry->native_commands[idx].is_builtin && !allow_builtin_remove) {
        free(lookup_key);
        return false;
    }

    for (size_t j = idx + 1; j < count; j++) {
        registry->native_commands[j - 1] = registry->native_commands[j];
    }
    arena_arr_set_len(registry->native_commands, count - 1);
    free(lookup_key);
    return eval_registry_index_rebuild(registry);
}

const Eval_Native_Command *eval_native_cmd_find_const(const EvalExecContext *ctx, String_View name) {
    if (!ctx || !ctx->registry) return NULL;
    return eval_registry_find_const(ctx->registry, name);
}

Eval_Native_Command *eval_native_cmd_find(EvalExecContext *ctx, String_View name) {
    return (Eval_Native_Command*)eval_native_cmd_find_const(ctx, name);
}

bool eval_native_cmd_register_internal(EvalExecContext *ctx,
                                       const EvalNativeCommandDef *def,
                                       bool is_builtin,
                                       bool allow_during_run) {
    if (!ctx || !ctx->registry || !def || !def->handler || def->name.count == 0 || !def->name.data) return false;
    if ((!allow_during_run && ctx->file_eval_depth > 0) || ctx->registry->mutation_blocked) return false;
    if (eval_user_cmd_find(ctx, def->name)) return false;
    return eval_registry_register_internal(ctx->registry, def, is_builtin);
}

bool eval_native_cmd_unregister_internal(EvalExecContext *ctx,
                                         String_View name,
                                         bool allow_builtin_remove,
                                         bool allow_during_run) {
    if (!ctx || !ctx->registry || name.count == 0 || !name.data) return false;
    if ((!allow_during_run && ctx->file_eval_depth > 0) || ctx->registry->mutation_blocked) return false;
    return eval_registry_unregister_internal(ctx->registry, name, allow_builtin_remove);
}

// -----------------------------------------------------------------------------
// Scope stack management
// -----------------------------------------------------------------------------

bool eval_scope_push(EvalExecContext *ctx) {
    if (!ctx) return false;
    Eval_Scope_State *scope = eval_scope_slice(ctx);
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth < arena_arr_len(scope->scopes)) {
        Var_Scope *s = &scope->scopes[depth];
        if (s->vars) {
            stbds_shfree(s->vars);
            s->vars = NULL;
        }
    } else {
        if (!EVAL_ARR_PUSH(ctx, ctx->event_arena, scope->scopes, ((Var_Scope){0}))) return false;
    }
    scope->visible_scope_depth = depth + 1;
    Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
    s->vars = NULL;
    return true;
}

void eval_scope_pop(EvalExecContext *ctx) {
    if (ctx && eval_scope_visible_depth(ctx) > 1) {
        Eval_Scope_State *scope = eval_scope_slice(ctx);
        Var_Scope *s = &scope->scopes[eval_scope_visible_depth(ctx) - 1];
        if (s->vars) {
            stbds_shfree(s->vars);
            s->vars = NULL;
        }
        scope->visible_scope_depth--;
    }
}

static String_View detect_compiler_id(void) {
#if defined(__clang__)
    return nob_sv_from_cstr("Clang");
#elif defined(_MSC_VER)
    return nob_sv_from_cstr("MSVC");
#elif defined(__GNUC__)
    return nob_sv_from_cstr("GNU");
#else
    return nob_sv_from_cstr("Unknown");
#endif
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

static bool eval_attach_sub_arena(Arena *owner, Arena **out_arena) {
    if (!owner || !out_arena) return false;
    *out_arena = arena_create(4096);
    if (!*out_arena) return false;
    if (!arena_on_destroy(owner, destroy_sub_arena_cb, *out_arena)) {
        arena_destroy(*out_arena);
        *out_arena = NULL;
        return false;
    }
    return true;
}

static const char *eval_copy_cstr_to_arena(Arena *arena, const char *src) {
    if (!arena || !src) return NULL;
    return arena_strndup(arena, src, strlen(src));
}

static void eval_scope_state_reset_for_session(Eval_Scope_State *scope) {
    if (!scope) return;

    if (scope->scopes && arena_arr_len(scope->scopes) > 1) {
        for (size_t i = 1; i < arena_arr_len(scope->scopes); i++) {
            if (scope->scopes[i].vars) {
                stbds_shfree(scope->scopes[i].vars);
                scope->scopes[i].vars = NULL;
            }
        }
        arena_arr_set_len(scope->scopes, 1);
    }

    scope->visible_scope_depth = arena_arr_len(scope->scopes) > 0 ? 1 : 0;
    if (scope->macro_frames) arena_arr_set_len(scope->macro_frames, 0);
    if (scope->block_frames) arena_arr_set_len(scope->block_frames, 0);
    if (scope->return_propagate_vars) arena_arr_set_len(scope->return_propagate_vars, 0);
}

static void eval_exec_load_session_state(EvalExecContext *ctx, EvalSession *session) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    if (!session) return;

    ctx->session = session;
    ctx->event_arena = session->persistent_arena;
    ctx->transaction_arena = session->state.transaction_arena;
    ctx->registry = session->state.registry;
    ctx->services = session->state.services;
    ctx->scope_state = session->state.scope_state;
    ctx->semantic_state = session->state.semantic_state;
    ctx->command_state = session->state.command_state;
    ctx->process_state = session->state.process_state;
    ctx->canonical_state = session->state.canonical_state;
    ctx->property_definitions = session->state.property_definitions;
    ctx->policy_levels = session->state.policy_levels;
    ctx->visible_policy_depth = session->state.visible_policy_depth;
    ctx->runtime_state = session->state.runtime_state;
    ctx->cpack_module_loaded = session->state.cpack_module_loaded;
    ctx->cpack_component_module_loaded = session->state.cpack_component_module_loaded;
    ctx->fetchcontent_module_loaded = session->state.fetchcontent_module_loaded;
    ctx->source_dir = session->source_root;
    ctx->binary_dir = session->binary_root;
    ctx->return_context = EVAL_RETURN_CTX_TOPLEVEL;
}

static void eval_exec_prepare_session_view(EvalExecContext *ctx, EvalSession *session) {
    eval_exec_load_session_state(ctx, session);
    if (!ctx || !session) return;

    ctx->arena = session->persistent_arena;
    ctx->stream = NULL;
    ctx->current_file = NULL;
    ctx->file_eval_depth = 0;
    ctx->function_eval_depth = 0;
    ctx->active_transaction = NULL;
    ctx->mode = EVAL_EXEC_MODE_PROJECT;
    ctx->oom = false;
    ctx->stop_requested = false;
}

static void eval_session_commit_state_from_exec(EvalSession *session, const EvalExecContext *exec) {
    if (!session || !exec) return;

    session->state.registry = exec->registry;
    session->state.services = exec->services;
    session->state.scope_state = exec->scope_state;
    eval_scope_state_reset_for_session(&session->state.scope_state);
    session->state.semantic_state = exec->semantic_state;
    session->state.command_state = exec->command_state;
    session->state.process_state = exec->process_state;
    session->state.canonical_state = exec->canonical_state;
    session->state.property_definitions = exec->property_definitions;
    session->state.policy_levels = exec->policy_levels;
    session->state.visible_policy_depth = exec->visible_policy_depth;
    session->state.runtime_state.compat_profile = exec->runtime_state.compat_profile;
    session->state.runtime_state.unsupported_policy = exec->runtime_state.unsupported_policy;
    session->state.runtime_state.error_budget = exec->runtime_state.error_budget;
    session->state.runtime_state.continue_on_error_snapshot =
        (exec->runtime_state.compat_profile == EVAL_PROFILE_PERMISSIVE);
    session->state.runtime_state.run_report = (Eval_Run_Report){0};
    session->state.runtime_state.in_variable_watch_notification = false;
    session->state.transaction_arena = exec->transaction_arena;
    session->state.cpack_module_loaded = exec->cpack_module_loaded;
    session->state.cpack_component_module_loaded = exec->cpack_component_module_loaded;
    session->state.fetchcontent_module_loaded = exec->fetchcontent_module_loaded;
}

static bool eval_push_root_exec_context(EvalExecContext *ctx) {
    if (!ctx) return false;

    Eval_Exec_Context root = {0};
    root.kind = EVAL_EXEC_CTX_ROOT;
    root.return_context = EVAL_RETURN_CTX_TOPLEVEL;
    root.source_dir = ctx->source_dir;
    root.binary_dir = ctx->binary_dir;
    root.list_dir = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR));
    if (root.list_dir.count == 0) root.list_dir = ctx->source_dir;
    root.current_file = ctx->current_file;
    return eval_exec_push(ctx, root);
}

static bool eval_exec_bind_request(EvalExecContext *ctx,
                                   EvalSession *session,
                                   Arena *scratch_arena,
                                   Event_Stream *stream,
                                   String_View source_dir,
                                   String_View binary_dir,
                                   const char *list_file,
                                   Eval_Exec_Mode mode) {
    if (!ctx || !session || !scratch_arena || !stream) return false;

    String_View requested_source = source_dir.count > 0 ? source_dir : session->source_root;
    String_View requested_binary = binary_dir.count > 0 ? binary_dir : session->binary_root;
    if (requested_source.count == 0) requested_source = nob_sv_from_cstr(".");
    if (requested_binary.count == 0) requested_binary = requested_source;

    ctx->session = session;
    ctx->arena = scratch_arena;
    ctx->stream = stream;
    ctx->source_dir = sv_copy_to_event_arena(ctx, requested_source);
    if (requested_source.count > 0 && ctx->source_dir.count == 0) return false;
    ctx->binary_dir = sv_copy_to_event_arena(ctx, requested_binary);
    if (requested_binary.count > 0 && ctx->binary_dir.count == 0) return false;
    ctx->mode = mode;
    if (list_file) {
        ctx->current_file = eval_copy_cstr_to_arena(ctx->event_arena, list_file);
        if (!ctx->current_file) return false;
    } else {
        ctx->current_file = NULL;
    }

    ctx->scope_state.visible_scope_depth = 1;
    ctx->scope_state.macro_frames = NULL;
    ctx->scope_state.block_frames = NULL;
    ctx->scope_state.return_propagate_vars = NULL;
    ctx->message_check_stack = NULL;
    if (ctx->exec_contexts) {
        arena_arr_set_len(ctx->exec_contexts, 0);
    } else {
        ctx->exec_contexts = NULL;
    }
    ctx->visible_policy_depth = 0;
    ctx->file_eval_depth = 0;
    ctx->function_eval_depth = 0;
    ctx->return_context = EVAL_RETURN_CTX_TOPLEVEL;
    ctx->active_transaction = NULL;
    eval_clear_return_state(ctx);
    eval_reset_stop_request(ctx);

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"), ctx->source_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"), ctx->binary_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), ctx->source_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), ctx->binary_dir)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), ctx->source_dir)) return false;
    if (!eval_directory_register_known(ctx, ctx->source_dir)) return false;
    {
        String_View cmakefiles_dir = eval_sv_path_join(ctx->event_arena, ctx->binary_dir, nob_sv_from_cstr("CMakeFiles"));
        String_View redirects_dir = eval_sv_path_join(ctx->event_arena, cmakefiles_dir, nob_sv_from_cstr("pkgRedirects"));
        if (eval_should_stop(ctx)) return false;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_FIND_PACKAGE_REDIRECTS_DIR"), redirects_dir)) return false;
    }
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE),
                              ctx->current_file ? nob_sv_from_cstr(ctx->current_file) : nob_sv_from_cstr(""))) {
        return false;
    }
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr("0"))) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_POLICY_STACK_DEPTH), nob_sv_from_cstr("1"))) return false;

    if (ctx->file_state.deferred_dirs) {
        arena_arr_set_len(ctx->file_state.deferred_dirs, 0);
    }
    return true;
}

static Eval_Result eval_context_run_prepared(EvalExecContext *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    if (!eval_push_root_exec_context(ctx)) return eval_result_fatal();
    if (!eval_exec_publish_current_vars(ctx)) {
        eval_exec_pop(ctx);
        return eval_result_fatal();
    }
    if (!eval_defer_push_directory(ctx, eval_current_source_dir(ctx), eval_current_binary_dir(ctx))) {
        eval_exec_pop(ctx);
        return eval_result_fatal();
    }
    size_t entered_file_depth = ctx->file_eval_depth;
    eval_report_reset(ctx);
    Eval_Result result = eval_execute_node_list(ctx, &ast);
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_defer_flush_current_directory(ctx)) result = eval_result_fatal();
    }
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_file_generate_flush(ctx)) result = eval_result_fatal();
    }
    if (!eval_result_is_fatal(result) && !eval_should_stop(ctx)) {
        if (!eval_finalize_cpack_package_snapshot(ctx)) result = eval_result_fatal();
    }
    eval_clear_return_state(ctx);
    if (!eval_defer_pop_directory(ctx)) result = eval_result_fatal();
    eval_file_lock_release_file_scope(ctx, entered_file_depth);
    eval_exec_pop(ctx);
    eval_report_finalize(ctx);
    result = eval_result_merge(result, eval_result_ok_if_running(ctx));
    if (!eval_result_is_fatal(result) && ctx->runtime_state.run_report.error_count > 0) {
        result = eval_result_merge(result, eval_result_soft_error());
    }
    return result;
}

static bool eval_seed_compile_feature_vars(EvalExecContext *ctx) {
    if (!ctx) return false;

    static const char *const c_known =
        "c_std_90;c_std_99;c_std_11;c_std_17;c_std_23;"
        "c_function_prototypes;c_restrict;c_static_assert;c_variadic_macros";
    static const char *const cxx_known =
        "cxx_std_98;cxx_std_11;cxx_std_14;cxx_std_17;cxx_std_20;cxx_std_23;"
        "cxx_alias_templates;cxx_constexpr;cxx_decltype;cxx_final;cxx_generic_lambdas;"
        "cxx_lambdas;cxx_nullptr;cxx_range_for;cxx_rvalue_references;cxx_static_assert;"
        "cxx_variadic_templates";
    static const char *const cuda_known =
        "cuda_std_03;cuda_std_11;cuda_std_14;cuda_std_17;cuda_std_20";

    return eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_KNOWN_FEATURES"), nob_sv_from_cstr(c_known)) &&
           eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_COMPILE_FEATURES"), nob_sv_from_cstr(c_known)) &&
           eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_KNOWN_FEATURES"), nob_sv_from_cstr(cxx_known)) &&
           eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILE_FEATURES"), nob_sv_from_cstr(cxx_known)) &&
           eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CUDA_KNOWN_FEATURES"), nob_sv_from_cstr(cuda_known)) &&
           eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CUDA_COMPILE_FEATURES"), nob_sv_from_cstr(cuda_known));
}

static EvalSession *eval_session_create_impl(const EvalSession_Config *cfg) {
    if (!cfg || !cfg->persistent_arena) return NULL;

#define EVAL_SESSION_CREATE_REQUIRE(expr, label)                                                      \
    do {                                                                                              \
        if (!(expr)) {                                                                                \
            return NULL;                                                                              \
        }                                                                                             \
    } while (0)

    EvalSession *session = arena_alloc_zero(cfg->persistent_arena, sizeof(EvalSession));
    EVAL_SESSION_CREATE_REQUIRE(session, "alloc session");

    String_View source_root = cfg->source_root.count > 0 ? cfg->source_root : nob_sv_from_cstr(".");
    String_View binary_root = cfg->binary_root.count > 0 ? cfg->binary_root : source_root;
    session->persistent_arena = cfg->persistent_arena;
    session->source_root = sv_copy_to_arena(cfg->persistent_arena, source_root);
    session->binary_root = sv_copy_to_arena(cfg->persistent_arena, binary_root);
    session->enable_export_host_effects = cfg->enable_export_host_effects;
    EVAL_SESSION_CREATE_REQUIRE(!(source_root.count > 0 && session->source_root.count == 0), "copy source_root");
    EVAL_SESSION_CREATE_REQUIRE(!(binary_root.count > 0 && session->binary_root.count == 0), "copy binary_root");

    session->state.services = cfg->services;
    session->state.registry = cfg->registry;
    if (!session->state.registry) {
        session->state.registry = eval_registry_create(cfg->persistent_arena);
        EVAL_SESSION_CREATE_REQUIRE(session->state.registry, "create registry");
        session->owns_registry = true;
    }
    EVAL_SESSION_CREATE_REQUIRE(eval_dispatcher_seed_builtin_commands(session->state.registry), "seed builtins");

    session->state.runtime_state.compat_profile = EVAL_PROFILE_PERMISSIVE;
    if (cfg->compat_profile == EVAL_PROFILE_STRICT || cfg->compat_profile == EVAL_PROFILE_CI_STRICT) {
        session->state.runtime_state.compat_profile = cfg->compat_profile;
    }
    session->state.runtime_state.unsupported_policy = EVAL_UNSUPPORTED_WARN;
    session->state.runtime_state.error_budget = 0;
    session->state.runtime_state.continue_on_error_snapshot =
        (session->state.runtime_state.compat_profile == EVAL_PROFILE_PERMISSIVE);
    session->state.runtime_state.run_report = (Eval_Run_Report){0};
    session->state.runtime_state.in_variable_watch_notification = false;

    EVAL_SESSION_CREATE_REQUIRE(eval_attach_sub_arena(cfg->persistent_arena, &session->state.transaction_arena), "attach tx arena");
    EVAL_SESSION_CREATE_REQUIRE(eval_attach_sub_arena(cfg->persistent_arena, &session->state.semantic_state.targets.arena),
                                "attach targets arena");
    EVAL_SESSION_CREATE_REQUIRE(eval_attach_sub_arena(cfg->persistent_arena, &session->state.semantic_state.tests.arena),
                                "attach tests arena");
    EVAL_SESSION_CREATE_REQUIRE(eval_attach_sub_arena(cfg->persistent_arena, &session->state.command_state.user_commands_arena),
                                "attach user_commands arena");

    EvalExecContext ctx_storage = {0};
    EvalExecContext *ctx = &ctx_storage;
    eval_exec_prepare_session_view(ctx, session);
    eval_clear_return_state(ctx);
    eval_report_reset(ctx);

    EVAL_SESSION_CREATE_REQUIRE(EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->scope_state.scopes, ((Var_Scope){0})),
                                "push global scope");
    ctx->scope_state.visible_scope_depth = 1;

    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"), ctx->source_dir),
                                "set CMAKE_SOURCE_DIR");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"), ctx->binary_dir),
                                "set CMAKE_BINARY_DIR");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR), ctx->source_dir),
                                "set CURRENT_SOURCE_DIR");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR), ctx->binary_dir),
                                "set CURRENT_BINARY_DIR");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR), ctx->source_dir),
                                "set CURRENT_LIST_DIR");
    EVAL_SESSION_CREATE_REQUIRE(eval_directory_register_known(ctx, ctx->source_dir), "register known dir");
    {
        String_View cmakefiles_dir = eval_sv_path_join(ctx->event_arena, ctx->binary_dir, nob_sv_from_cstr("CMakeFiles"));
        String_View redirects_dir = eval_sv_path_join(ctx->event_arena, cmakefiles_dir, nob_sv_from_cstr("pkgRedirects"));
        EVAL_SESSION_CREATE_REQUIRE(!eval_should_stop(ctx), "build redirects dir");
        EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx,
                                                         nob_sv_from_cstr("CMAKE_FIND_PACKAGE_REDIRECTS_DIR"),
                                                         redirects_dir),
                                    "set redirects dir");
    }
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE), nob_sv_from_cstr("")),
                                "set CURRENT_LIST_FILE");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CURRENT_LIST_LINE"), nob_sv_from_cstr("0")),
                                "set CURRENT_LIST_LINE");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx,
                                                     nob_sv_from_cstr(EVAL_VAR_NOBIFY_POLICY_STACK_DEPTH),
                                                     nob_sv_from_cstr("1")),
                                "set POLICY_STACK_DEPTH");
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_POLICY_VERSION"), nob_sv_from_cstr("")),
                                "set CMAKE_POLICY_VERSION");

#if defined(_WIN32)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("1"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("0"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("WIN32"), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("UNIX"), nob_sv_from_cstr("1"))) return NULL;
#endif

#if defined(__APPLE__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("APPLE"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MSVC"), nob_sv_from_cstr("0"))) return NULL;
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("1"))) return NULL;
#else
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("MINGW"), nob_sv_from_cstr("0"))) return NULL;
#endif

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_VERSION"), nob_sv_from_cstr("3.28.0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_MAJOR_VERSION"), nob_sv_from_cstr("3"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_MINOR_VERSION"), nob_sv_from_cstr("28"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_PATCH_VERSION"), nob_sv_from_cstr("0"))) return NULL;
    {
        String_View host_system_name = eval_detect_host_system_name();
        String_View host_processor = eval_detect_host_processor();
        String_View host_system_version = eval_host_os_version_temp(ctx);
        if (eval_should_stop(ctx)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_NAME"), host_system_name)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"), host_system_name)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM_PROCESSOR"), host_processor)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_PROCESSOR"), host_processor)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_SYSTEM"), host_system_name)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM"), host_system_name)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_VERSION"), host_system_version)) return NULL;
    }
    EVAL_SESSION_CREATE_REQUIRE(eval_var_set_current(ctx,
                                                     nob_sv_from_cstr("CMAKE_COMMAND"),
                                                     eval_default_cmake_command_temp(ctx)),
                                "set CMAKE_COMMAND");

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_NAME"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("PROJECT_VERSION"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr(EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR),
                              ctx->runtime_state.compat_profile == EVAL_PROFILE_PERMISSIVE
                                  ? nob_sv_from_cstr("1")
                                  : nob_sv_from_cstr("0"))) {
        return NULL;
    }
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr(EVAL_VAR_NOBIFY_COMPAT_PROFILE),
                              eval_compat_profile_to_sv(ctx->runtime_state.compat_profile))) {
        return NULL;
    }
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_ERROR_BUDGET), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_UNSUPPORTED_POLICY), nob_sv_from_cstr("WARN"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_FILE_GLOB_STRICT), nob_sv_from_cstr("0"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(EVAL_VAR_NOBIFY_WHILE_MAX_ITERATIONS), nob_sv_from_cstr("10000"))) return NULL;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES"), nob_sv_from_cstr(""))) return NULL;
    if (!eval_property_engine_set(ctx,
                                  nob_sv_from_cstr("GLOBAL"),
                                  nob_sv_from_cstr(""),
                                  nob_sv_from_cstr("ENABLED_LANGUAGES"),
                                  nob_sv_from_cstr(""))) {
        return NULL;
    }
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("NOBIFY_PROPERTY_GLOBAL::ENABLED_LANGUAGES"),
                              nob_sv_from_cstr(""))) {
        return NULL;
    }

    {
        const char *cc = eval_getenv_temp(ctx, "CC");
        const char *cxx = eval_getenv_temp(ctx, "CXX");
        if (!cc || cc[0] == '\0') cc = "cc";
        if (!cxx || cxx[0] == '\0') cxx = "c++";
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER"), nob_sv_from_cstr(cc))) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"), nob_sv_from_cstr(cxx))) return NULL;
    }

    {
        String_View compiler_id = detect_compiler_id();
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_ID"), compiler_id)) return NULL;
        if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"), compiler_id)) return NULL;
    }
    if (!eval_seed_compile_feature_vars(ctx)) return NULL;

    eval_session_commit_state_from_exec(session, ctx);
    return session;

#undef EVAL_SESSION_CREATE_REQUIRE
}

EvalRegistry *eval_registry_create(Arena *arena) {
    if (!arena) return NULL;
    EvalRegistry *registry = arena_alloc_zero(arena, sizeof(EvalRegistry));
    if (!registry) return NULL;
    registry->arena = arena;
    if (!arena_on_destroy(arena, eval_registry_cleanup_cb, registry)) return NULL;
    if (!eval_dispatcher_seed_builtin_commands(registry)) {
        eval_registry_destroy(registry);
        return NULL;
    }
    return registry;
}

void eval_registry_destroy(EvalRegistry *registry) {
    if (!registry) return;
    if (registry->native_command_index) {
        stbds_shfree(registry->native_command_index);
        registry->native_command_index = NULL;
    }
    registry->builtins_seeded = false;
}

bool eval_registry_register_native_command(EvalRegistry *registry,
                                           const EvalNativeCommandDef *def) {
    return eval_registry_register_internal(registry, def, false);
}

bool eval_registry_unregister_native_command(EvalRegistry *registry,
                                             String_View command_name) {
    return eval_registry_unregister_internal(registry, command_name, false);
}

bool eval_registry_get_command_capability(const EvalRegistry *registry,
                                          String_View command_name,
                                          Command_Capability *out_capability) {
    if (!out_capability) return false;
    const Eval_Native_Command *cmd = eval_registry_find_const(registry, command_name);
    out_capability->command_name = command_name;
    if (!cmd) {
        out_capability->implemented_level = EVAL_CMD_IMPL_MISSING;
        out_capability->fallback_behavior = EVAL_FALLBACK_NOOP_WARN;
        return false;
    }
    out_capability->implemented_level = cmd->implemented_level;
    out_capability->fallback_behavior = cmd->fallback_behavior;
    return true;
}

EvalSession *eval_session_create(const EvalSession_Config *cfg) {
    return eval_session_create_impl(cfg);
}

void eval_session_destroy(EvalSession *session) {
    if (!session) return;

    EvalSessionState *state = &session->state;
    if (state->scope_state.cache_entries) {
        stbds_shfree(state->scope_state.cache_entries);
        state->scope_state.cache_entries = NULL;
    }
    if (state->process_state.env_overrides) {
        stbds_shfree(state->process_state.env_overrides);
        state->process_state.env_overrides = NULL;
    }
    for (size_t i = 0; i < arena_arr_len(state->scope_state.scopes); i++) {
        if (state->scope_state.scopes[i].vars) {
            stbds_shfree(state->scope_state.scopes[i].vars);
            state->scope_state.scopes[i].vars = NULL;
        }
    }
    if (session->owns_registry && state->registry) {
        eval_registry_destroy(state->registry);
        state->registry = NULL;
    }
}

EvalRunResult eval_session_run(EvalSession *session,
                               const EvalExec_Request *request,
                               Ast_Root ast) {
    EvalRunResult out = {0};
    out.result = eval_result_fatal();
    if (!session || !request || !request->scratch_arena) return out;

    Event_Stream *effective_stream = request->stream ? request->stream : event_stream_create(request->scratch_arena);
    if (!effective_stream) return out;

    size_t emitted_before = request->stream ? request->stream->count : 0;
    EvalExecContext exec = {0};
    eval_exec_load_session_state(&exec, session);
    if (session->state.registry) session->state.registry->mutation_blocked = true;
    if (!eval_exec_bind_request(&exec,
                                session,
                                request->scratch_arena,
                                effective_stream,
                                request->source_dir,
                                request->binary_dir,
                                request->list_file,
                                request->mode)) {
        if (session->state.registry) session->state.registry->mutation_blocked = false;
        return out;
    }

    out.result = eval_context_run_prepared(&exec, ast);
    out.report = exec.runtime_state.run_report;
    out.emitted_event_count = request->stream ? (request->stream->count - emitted_before) : 0;
    session->last_run_report = out.report;
    if (session->state.registry) session->state.registry->mutation_blocked = false;
    eval_session_commit_state_from_exec(session, &exec);
    return out;
}

bool eval_session_set_compat_profile(EvalSession *session, Eval_Compat_Profile profile) {
    if (!session) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, session);
    if (!eval_compat_set_profile(&exec, profile)) return false;
    eval_session_commit_state_from_exec(session, &exec);
    return true;
}

bool eval_session_register_native_command(EvalSession *session,
                                          const EvalNativeCommandDef *def) {
    if (!session) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, session);
    return eval_native_cmd_register_internal(&exec, def, false, false);
}

bool eval_session_unregister_native_command(EvalSession *session,
                                            String_View command_name) {
    if (!session) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, session);
    return eval_native_cmd_unregister_internal(&exec, command_name, false, false);
}

bool eval_session_command_exists(const EvalSession *session, String_View command_name) {
    if (!session) return false;
    if (eval_registry_find_const(session->state.registry, command_name)) return true;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, (EvalSession*)session);
    return eval_user_cmd_find(&exec, command_name) != NULL;
}

Eval_Result eval_run_ast_inline(EvalExecContext *ctx, Ast_Root ast) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    return eval_execute_node_list(ctx, &ast);
}

bool eval_session_get_visible_var(const EvalSession *session,
                                  String_View key,
                                  String_View *out_value) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (!session || !out_value) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, (EvalSession*)session);
    *out_value = eval_var_get_visible(&exec, key);
    return eval_var_defined_visible(&exec, key);
}

bool eval_session_cache_defined(const EvalSession *session, String_View key) {
    if (!session || key.count == 0) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, (EvalSession*)session);
    return eval_cache_defined(&exec, key);
}

bool eval_session_target_known(const EvalSession *session, String_View target_name) {
    if (!session || target_name.count == 0) return false;
    EvalExecContext exec = {0};
    eval_exec_prepare_session_view(&exec, (EvalSession*)session);
    return eval_target_visible(&exec, target_name);
}

size_t eval_session_canonical_artifact_count(const EvalSession *session) {
    if (!session) return 0;
    return arena_arr_len(session->state.canonical_state.artifacts);
}

bool eval_session_find_canonical_artifact(const EvalSession *session,
                                          String_View producer,
                                          String_View kind,
                                          String_View *out_primary_path) {
    const Eval_Canonical_Artifact_List artifacts = session ? session->state.canonical_state.artifacts : NULL;

    if (out_primary_path) *out_primary_path = nob_sv_from_cstr("");
    if (!session) return false;

    for (size_t i = arena_arr_len(artifacts); i-- > 0;) {
        if (!nob_sv_eq(artifacts[i].producer, producer)) continue;
        if (!nob_sv_eq(artifacts[i].kind, kind)) continue;
        if (out_primary_path) *out_primary_path = artifacts[i].primary_path;
        return true;
    }
    return false;
}

size_t eval_session_ctest_step_count(const EvalSession *session) {
    if (!session) return 0;
    return arena_arr_len(session->state.canonical_state.ctest_steps);
}

bool eval_session_find_ctest_step(const EvalSession *session,
                                  String_View command_name,
                                  String_View *out_status,
                                  String_View *out_submit_part) {
    const Eval_Ctest_Step_Record_List ctest_steps = session ? session->state.canonical_state.ctest_steps : NULL;

    if (out_status) *out_status = nob_sv_from_cstr("");
    if (out_submit_part) *out_submit_part = nob_sv_from_cstr("");
    if (!session) return false;

    for (size_t i = arena_arr_len(ctest_steps); i-- > 0;) {
        if (!nob_sv_eq(ctest_steps[i].command_name, command_name)) continue;
        if (out_status) *out_status = ctest_steps[i].status;
        if (out_submit_part) *out_submit_part = ctest_steps[i].submit_part;
        return true;
    }
    return false;
}

const EvalServices *eval_exec_services(const EvalExecContext *exec) {
    return exec ? exec->services : NULL;
}

Event_Origin eval_exec_origin_from_node(const EvalExecContext *exec, const Node *node) {
    return eval_origin_from_node(exec, node);
}

bool eval_exec_get_visible_var(const EvalExecContext *exec,
                               String_View key,
                               String_View *out_value) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (!exec || !out_value) return false;
    *out_value = eval_var_get_visible((EvalExecContext*)exec, key);
    return eval_var_defined_visible((EvalExecContext*)exec, key);
}

bool eval_exec_set_current_var(EvalExecContext *exec,
                               String_View key,
                               String_View value) {
    return exec ? eval_var_set_current(exec, key, value) : false;
}

bool eval_exec_unset_current_var(EvalExecContext *exec, String_View key) {
    return exec ? eval_var_unset_current(exec, key) : false;
}

Eval_Result eval_exec_emit_diag(EvalExecContext *exec,
                                Event_Diag_Severity severity,
                                Eval_Diag_Code code,
                                String_View component,
                                String_View command,
                                Event_Origin origin,
                                String_View cause,
                                String_View hint) {
    return eval_emit_diag_with_severity(exec, severity, code, component, command, origin, cause, hint);
}
