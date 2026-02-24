// --- START OF FILE evaluator_internal.h ---
#ifndef EVALUATOR_INTERNAL_H_
#define EVALUATOR_INTERNAL_H_

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"
#include "arena.h"
#include "parser.h"
#include "evaluator.h"
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    String_View *items;
    size_t count;
    size_t capacity;
} SV_List;

typedef struct {
    String_View key;
    String_View value;
    UT_hash_handle hh;
} Var_Binding;

typedef struct Var_Scope {
    Var_Binding *vars;
} Var_Scope;

typedef enum {
    USER_CMD_FUNCTION = 0,
    USER_CMD_MACRO,
} User_Command_Kind;

typedef struct {
    String_View name;
    User_Command_Kind kind;
    String_View *params;
    size_t param_count;
    Node_List body;
} User_Command;

typedef struct {
    User_Command *items;
    size_t count;
    size_t capacity;
} User_Command_List;

typedef struct {
    Var_Binding *items;
    size_t count;
    size_t capacity;
} Macro_Binding_List;

typedef struct {
    Macro_Binding_List bindings;
} Macro_Frame;

typedef struct {
    Macro_Frame *items;
    size_t count;
    size_t capacity;
} Macro_Frame_Stack;

struct Evaluator_Context {
    Arena *arena;          // TEMP ARENA: Limpa a cada statement (usado p/ expansão de args)
    Arena *event_arena;    // PERSISTENT ARENA: Sobrevive até o Build Model (usado p/ eventos)
    Arena *known_targets_arena;
    Arena *user_commands_arena;
    Cmake_Event_Stream *stream;

    String_View source_dir;
    String_View binary_dir;

    const char *current_file;

    Var_Scope *scopes;
    size_t scope_depth;
    size_t scope_capacity;

    SV_List known_targets; 
    User_Command_List user_commands;
    Macro_Frame_Stack macro_frames;

    size_t loop_depth;
    bool break_requested;
    bool continue_requested;
    bool return_requested;

    bool oom;
    bool stop_requested;
};

// ---- controle e memória ----
bool eval_should_stop(Evaluator_Context *ctx);
void eval_request_stop(Evaluator_Context *ctx);
void eval_request_stop_on_error(Evaluator_Context *ctx);
bool eval_continue_on_error(Evaluator_Context *ctx);
bool ctx_oom(Evaluator_Context *ctx);

static inline bool eval_mark_oom_if_null(Evaluator_Context *ctx, const void *ptr) {
    if (ptr) return false;
    ctx_oom(ctx);
    return true;
}

#define EVAL_OOM_RETURN_IF_NULL(ctx, ptr, ret_value) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return (ret_value); } while (0)

#define EVAL_OOM_RETURN_VOID_IF_NULL(ctx, ptr) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return; } while (0)

Arena *eval_temp_arena(Evaluator_Context *ctx);
Arena *eval_event_arena(Evaluator_Context *ctx);

String_View sv_copy_to_temp_arena(Evaluator_Context *ctx, String_View sv);
String_View sv_copy_to_event_arena(Evaluator_Context *ctx, String_View sv);
String_View sv_copy_to_arena(Arena *arena, String_View sv);
char *eval_sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv);

// ---- rastreio de origem (NOVO) ----
Cmake_Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node);

// ---- args e expansão (NOVO COMPORTAMENTO) ----
// Retorna a lista expandida e dividida por ';' (alocada na TEMP ARENA)
SV_List eval_resolve_args(Evaluator_Context *ctx, const Args *raw_args);
SV_List eval_resolve_args_literal(Evaluator_Context *ctx, const Args *raw_args);

// ---- diagnostics / events ----
bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev);
bool eval_emit_diag(Evaluator_Context *ctx,
                    Cmake_Diag_Severity sev,
                    String_View component,
                    String_View command,
                    Cmake_Event_Origin origin,
                    String_View cause,
                    String_View hint);

// ---- vars ----
String_View eval_var_get(Evaluator_Context *ctx, String_View key);
bool eval_var_defined(Evaluator_Context *ctx, String_View key);
bool eval_var_set(Evaluator_Context *ctx, String_View key, String_View value);
bool eval_var_unset(Evaluator_Context *ctx, String_View key);
bool eval_var_defined_in_current_scope(Evaluator_Context *ctx, String_View key);

// ---- targets ----
bool eval_target_known(Evaluator_Context *ctx, String_View name);
bool eval_target_register(Evaluator_Context *ctx, String_View name);

// ---- user commands ----
bool eval_user_cmd_register(Evaluator_Context *ctx, const Node *node);
bool eval_user_cmd_invoke(Evaluator_Context *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin);
User_Command *eval_user_cmd_find(Evaluator_Context *ctx, String_View name);

// ---- utilitários compartilhados ----
bool eval_sv_key_eq(String_View a, String_View b);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
String_View eval_sv_join_semi_temp(Evaluator_Context *ctx, String_View *items, size_t count);
bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out);
bool eval_sv_is_abs_path(String_View p);
String_View eval_sv_path_join(Arena *arena, String_View a, String_View b);
const char *eval_getenv_temp(Evaluator_Context *ctx, const char *name);
bool eval_has_env(Evaluator_Context *ctx, const char *name);

// ---- macro argument substitution (textual) ----
bool eval_macro_frame_push(Evaluator_Context *ctx);
void eval_macro_frame_pop(Evaluator_Context *ctx);
bool eval_macro_bind_set(Evaluator_Context *ctx, String_View key, String_View value);
bool eval_macro_bind_get(Evaluator_Context *ctx, String_View key, String_View *out_value);

// ---- Gerenciamento de Escopo ----
bool eval_scope_push(Evaluator_Context *ctx);
void eval_scope_pop(Evaluator_Context *ctx);

// ---- Execução Externa (Subdiretórios e Includes) ----
// Retorna false em caso de OOM ou erro fatal.
bool eval_execute_file(Evaluator_Context *ctx, String_View file_path, bool is_add_subdirectory, String_View explicit_bin_dir);

#ifdef __cplusplus
}
#endif

#endif // EVALUATOR_INTERNAL_H_
