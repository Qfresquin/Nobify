// --- START OF FILE evaluator_internal.h ---
#ifndef EVALUATOR_INTERNAL_H_
#define EVALUATOR_INTERNAL_H_

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"
#include "arena.h"
#include "arena_dyn.h"
#include "parser.h"
#include "evaluator.h"
#include "eval_policy_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVAL_VAR_CURRENT_LIST_FILE "CMAKE_CURRENT_LIST_FILE"
#define EVAL_VAR_CURRENT_LIST_DIR "CMAKE_CURRENT_LIST_DIR"
#define EVAL_VAR_CURRENT_SOURCE_DIR "CMAKE_CURRENT_SOURCE_DIR"
#define EVAL_VAR_CURRENT_BINARY_DIR "CMAKE_CURRENT_BINARY_DIR"
#define EVAL_VAR_NOBIFY_POLICY_STACK_DEPTH "NOBIFY_POLICY_STACK_DEPTH"
#define EVAL_VAR_NOBIFY_CONTINUE_ON_ERROR "CMAKE_NOBIFY_CONTINUE_ON_ERROR"
#define EVAL_VAR_NOBIFY_COMPAT_PROFILE "CMAKE_NOBIFY_COMPAT_PROFILE"
#define EVAL_VAR_NOBIFY_ERROR_BUDGET "CMAKE_NOBIFY_ERROR_BUDGET"
#define EVAL_VAR_NOBIFY_UNSUPPORTED_POLICY "CMAKE_NOBIFY_UNSUPPORTED_POLICY"
#define EVAL_VAR_NOBIFY_FILE_GLOB_STRICT "CMAKE_NOBIFY_FILE_GLOB_STRICT"
#define EVAL_VAR_NOBIFY_WHILE_MAX_ITERATIONS "CMAKE_NOBIFY_WHILE_MAX_ITERATIONS"
#define EVAL_VAR_NOBIFY_EXPAND_MAX_RECURSION "CMAKE_NOBIFY_EXPAND_MAX_RECURSION"

#define EVAL_POLICY_CMP0017 "CMP0017"
#define EVAL_POLICY_CMP0036 "CMP0036"
#define EVAL_POLICY_CMP0048 "CMP0048"
#define EVAL_POLICY_CMP0061 "CMP0061"
#define EVAL_POLICY_CMP0074 "CMP0074"
#define EVAL_POLICY_CMP0077 "CMP0077"
#define EVAL_POLICY_CMP0102 "CMP0102"
#define EVAL_POLICY_CMP0124 "CMP0124"
#define EVAL_POLICY_CMP0126 "CMP0126"
#define EVAL_POLICY_CMP0140 "CMP0140"
#define EVAL_POLICY_CMP0144 "CMP0144"
#define EVAL_POLICY_CMP0152 "CMP0152"
#define EVAL_POLICY_CMP0153 "CMP0153"
#define EVAL_POLICY_CMP0174 "CMP0174"

typedef String_View *SV_List;

typedef struct {
    String_View key;
    String_View value;
} Var_Binding;

typedef struct Eval_Var_Entry {
    char *key;
    String_View value;
} Eval_Var_Entry;

typedef struct {
    String_View data;
    String_View type;
    String_View doc;
} Eval_Cache_Value;

typedef struct Eval_Cache_Entry {
    char *key;
    Eval_Cache_Value value;
} Eval_Cache_Entry;

typedef struct Var_Scope {
    Eval_Var_Entry *vars;
} Var_Scope;

typedef enum {
    USER_CMD_FUNCTION = 0,
    USER_CMD_MACRO,
} User_Command_Kind;

typedef struct {
    String_View name;
    User_Command_Kind kind;
    String_View *params;
    Node_List body;
} User_Command;

typedef User_Command *User_Command_List;

typedef struct {
    String_View name;
    String_View declared_dir;
} Eval_Target_Record;

typedef Eval_Target_Record *Eval_Target_Record_List;

typedef struct {
    String_View name;
    Eval_Native_Command_Handler handler;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
    char *normalized_name;
    bool is_builtin;
} Eval_Native_Command;

typedef Eval_Native_Command *Eval_Native_Command_List;

typedef struct Eval_Native_Command_Index_Entry {
    char *key;
    size_t value;
} Eval_Native_Command_Index_Entry;

typedef Var_Binding *Macro_Binding_List;

typedef struct {
    Macro_Binding_List bindings;
} Macro_Frame;

typedef Macro_Frame *Macro_Frame_Stack;

typedef struct {
    bool variable_scope_pushed;
    bool policy_scope_pushed;
    String_View *propagate_vars;
    bool propagate_on_return;
} Block_Frame;

typedef Block_Frame *Block_Frame_Stack;

typedef struct {
    int guard_kind;
    size_t owner_file_depth;
    size_t owner_function_depth;
    String_View path;
#if defined(_WIN32)
    void *handle;
#else
    int fd;
#endif
} Eval_File_Lock;

typedef Eval_File_Lock *Eval_File_Lock_List;

typedef struct {
    Cmake_Event_Origin origin;
    String_View command_name;
    String_View output_path;
    bool has_input;
    String_View input_path;
    bool has_content;
    String_View content;
    bool has_condition;
    String_View condition;
    bool has_target;
    String_View target;
    bool has_newline_style;
    String_View newline_style;
    bool use_source_permissions;
    bool no_source_permissions;
    bool has_file_permissions;
    unsigned int file_mode;
} Eval_File_Generate_Job;

typedef Eval_File_Generate_Job *Eval_File_Generate_Job_List;

typedef struct {
    unsigned char states[156];
} Eval_Policy_Level;

typedef struct {
    int major;
    int minor;
    int patch;
    int tweak;
} Eval_Semver;

typedef struct {
    unsigned long long total_virtual_mib;
    unsigned long long available_virtual_mib;
    unsigned long long total_physical_mib;
    unsigned long long available_physical_mib;
} Eval_Host_Memory_Info;

typedef enum {
    EVAL_CMDLINE_UNIX = 0,
    EVAL_CMDLINE_WINDOWS,
    EVAL_CMDLINE_NATIVE,
} Eval_Cmdline_Mode;

typedef struct {
    SV_List argv;
    String_View working_directory;
    String_View stdin_data;
    bool has_timeout;
    double timeout_seconds;
} Eval_Process_Run_Request;

typedef struct {
    String_View stdout_text;
    String_View stderr_text;
    String_View result_text;
    int exit_code;
    bool started;
    bool timed_out;
} Eval_Process_Run_Result;

typedef struct {
    String_View text;
    bool is_set;
} Eval_Process_Env_Value;

typedef struct {
    char *key;
    Eval_Process_Env_Value value;
} Eval_Process_Env_Entry;

typedef Eval_Process_Env_Entry *Eval_Process_Env_Table;

typedef struct {
    String_View scope_upper;
    String_View property_upper;
    bool inherited;
    bool has_brief_docs;
    String_View brief_docs;
    bool has_full_docs;
    String_View full_docs;
    bool has_initialize_from_variable;
    String_View initialize_from_variable;
} Eval_Property_Definition;

typedef Eval_Property_Definition *Eval_Property_Definition_List;

typedef enum {
    EVAL_PROP_QUERY_VALUE = 0,
    EVAL_PROP_QUERY_SET,
    EVAL_PROP_QUERY_DEFINED,
    EVAL_PROP_QUERY_BRIEF_DOCS,
    EVAL_PROP_QUERY_FULL_DOCS,
} Eval_Property_Query_Mode;

typedef struct {
    Cmake_Event_Origin origin;
    String_View id;
    String_View command_name;
    Args args;
} Eval_Deferred_Call;

typedef Eval_Deferred_Call *Eval_Deferred_Call_List;

typedef struct {
    String_View source_dir;
    String_View binary_dir;
    Eval_Deferred_Call_List calls;
} Eval_Deferred_Dir_Frame;

typedef Eval_Deferred_Dir_Frame *Eval_Deferred_Dir_Frame_Stack;

typedef enum {
    EVAL_RETURN_CTX_TOPLEVEL = 0,
    EVAL_RETURN_CTX_INCLUDE,
    EVAL_RETURN_CTX_FUNCTION,
    EVAL_RETURN_CTX_MACRO,
} Eval_Return_Context;

typedef struct {
    Var_Scope *scopes;
    // Invariant: visible_scope_depth <= arena_arr_len(scopes)
    size_t visible_scope_depth;
    Eval_Cache_Entry *cache_entries;
    Macro_Frame_Stack macro_frames;
    Block_Frame_Stack block_frames;
    String_View *return_propagate_vars;
} Eval_Scope_State;

typedef struct {
    Eval_Compat_Profile compat_profile;
    Eval_Unsupported_Policy unsupported_policy;
    size_t error_budget;
    // Snapshot refreshed at command-cycle boundary (eval_node entry).
    bool continue_on_error_snapshot;
    Eval_Run_Report run_report;
    bool in_variable_watch_notification;
} Eval_Runtime_State;

typedef struct {
    String_View name;
    String_View canonical_name;
    SV_List args;
} Eval_FetchContent_Declaration;

typedef Eval_FetchContent_Declaration *Eval_FetchContent_Declaration_List;

typedef struct {
    String_View name;
    String_View canonical_name;
    bool populated;
    String_View source_dir;
    String_View binary_dir;
} Eval_FetchContent_State;

typedef Eval_FetchContent_State *Eval_FetchContent_State_List;

typedef struct {
    Arena *native_commands_arena;
    Arena *known_targets_arena;
    Arena *user_commands_arena;
    Eval_Target_Record_List known_targets;
    SV_List alias_targets;
    Eval_Native_Command_List native_commands;
    // Case-insensitive lookup index: normalized command name -> native_commands index.
    Eval_Native_Command_Index_Entry *native_command_index;
    Eval_Var_Entry *property_store;
    User_Command_List user_commands;
    struct {
        String_View command_name;
        bool supports_find_package;
        bool supports_fetchcontent_makeavailable_serial;
        size_t active_find_package_depth;
        size_t active_fetchcontent_makeavailable_depth;
    } dependency_provider;
    SV_List active_find_packages;
    Eval_FetchContent_Declaration_List fetchcontent_declarations;
    Eval_FetchContent_State_List fetchcontent_states;
    SV_List active_fetchcontent_makeavailable;
    SV_List watched_variables;
    SV_List watched_variable_commands;
} Eval_Command_State;

typedef struct {
    Eval_File_Lock_List file_locks;
    Eval_File_Generate_Job_List file_generate_jobs;
    Eval_Deferred_Dir_Frame_Stack deferred_dirs;
    size_t next_deferred_call_id;
} Eval_File_State;

typedef struct {
    Eval_Process_Env_Table env_overrides;
} Eval_Process_State;

struct Evaluator_Context {
    Arena *arena;          // TEMP ARENA: Limpa a cada statement (usado p/ expansão de args)
    Arena *event_arena;    // PERSISTENT ARENA: storage interno do evaluator; Event_Stream owna payloads só no push
    Cmake_Event_Stream *stream;

    String_View source_dir;
    String_View binary_dir;

    const char *current_file;

    Eval_Scope_State scope_state;

    SV_List message_check_stack;
    Eval_Command_State command_state;
    Eval_File_State file_state;
    Eval_Process_State process_state;
    Eval_Property_Definition_List property_definitions;
    Eval_Policy_Level *policy_levels;
    // Invariant: visible_policy_depth <= arena_arr_len(policy_levels)
    size_t visible_policy_depth;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
    size_t file_eval_depth;
    size_t function_eval_depth;

    size_t loop_depth;
    bool break_requested;
    bool continue_requested;
    bool return_requested;
    Eval_Return_Context return_context;
    Eval_Runtime_State runtime_state;

    bool oom;
    bool stop_requested;
};

// ---- controle e memória ----
bool eval_should_stop(Evaluator_Context *ctx);
void eval_request_stop(Evaluator_Context *ctx);
void eval_request_stop_on_error(Evaluator_Context *ctx);
void eval_clear_stop_if_not_oom(Evaluator_Context *ctx);
bool eval_continue_on_error(Evaluator_Context *ctx);
void eval_refresh_runtime_compat(Evaluator_Context *ctx);
bool eval_compat_set_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
Cmake_Diag_Severity eval_compat_effective_severity(const Evaluator_Context *ctx, Cmake_Diag_Severity sev);
bool eval_compat_decide_on_diag(Evaluator_Context *ctx, Cmake_Diag_Severity effective_sev);
String_View eval_compat_profile_to_sv(Eval_Compat_Profile profile);
bool ctx_oom(Evaluator_Context *ctx);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id);

static inline Eval_Result eval_result_ok(void) {
    Eval_Result r = { .kind = EVAL_RESULT_OK };
    return r;
}

static inline Eval_Result eval_result_soft_error(void) {
    Eval_Result r = { .kind = EVAL_RESULT_SOFT_ERROR };
    return r;
}

static inline Eval_Result eval_result_fatal(void) {
    Eval_Result r = { .kind = EVAL_RESULT_FATAL };
    return r;
}

static inline Eval_Result eval_result_merge(Eval_Result lhs, Eval_Result rhs) {
    return (rhs.kind > lhs.kind) ? rhs : lhs;
}

static inline Eval_Result eval_result_from_bool(bool ok) {
    return ok ? eval_result_ok() : eval_result_fatal();
}

static inline Eval_Result eval_result_from_ctx(Evaluator_Context *ctx) {
    if (!ctx || eval_should_stop(ctx)) return eval_result_fatal();
    return ctx->runtime_state.run_report.error_count > 0 ? eval_result_soft_error() : eval_result_ok();
}

static inline Eval_Result eval_result_ok_if_running(Evaluator_Context *ctx) {
    return eval_should_stop(ctx) ? eval_result_fatal() : eval_result_ok();
}

static inline size_t eval_scope_visible_depth(const Evaluator_Context *ctx) {
    return ctx ? ctx->scope_state.visible_scope_depth : 0;
}

static inline size_t eval_policy_visible_depth(const Evaluator_Context *ctx) {
    return ctx ? ctx->visible_policy_depth : 0;
}

static inline String_View eval_policy_effective_id(Evaluator_Context *ctx, const char *policy_id) {
    if (!ctx || !policy_id) return nob_sv_from_cstr("");
    return eval_policy_get_effective(ctx, nob_sv_from_cstr(policy_id));
}

static inline bool eval_policy_is_new(Evaluator_Context *ctx, const char *policy_id) {
    return eval_sv_eq_ci_lit(eval_policy_effective_id(ctx, policy_id), "NEW");
}

static inline bool eval_policy_is_old(Evaluator_Context *ctx, const char *policy_id) {
    return eval_sv_eq_ci_lit(eval_policy_effective_id(ctx, policy_id), "OLD");
}

static inline bool eval_scope_use_parent_view(Evaluator_Context *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth <= 1) return false;
    *saved_depth = depth;
    ctx->scope_state.visible_scope_depth = depth - 1;
    return true;
}

static inline bool eval_scope_use_global_view(Evaluator_Context *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth == 0) return false;
    *saved_depth = depth;
    ctx->scope_state.visible_scope_depth = 1;
    return true;
}

static inline void eval_scope_restore_view(Evaluator_Context *ctx, size_t saved_depth) {
    if (ctx) ctx->scope_state.visible_scope_depth = saved_depth;
}

static inline Eval_Scope_State *eval_scope_slice(Evaluator_Context *ctx) {
    return ctx ? &ctx->scope_state : NULL;
}

static inline const Eval_Scope_State *eval_scope_slice_const(const Evaluator_Context *ctx) {
    return ctx ? &ctx->scope_state : NULL;
}

static inline Eval_Runtime_State *eval_runtime_slice(Evaluator_Context *ctx) {
    return ctx ? &ctx->runtime_state : NULL;
}

static inline const Eval_Runtime_State *eval_runtime_slice_const(const Evaluator_Context *ctx) {
    return ctx ? &ctx->runtime_state : NULL;
}

static inline Eval_Command_State *eval_command_slice(Evaluator_Context *ctx) {
    return ctx ? &ctx->command_state : NULL;
}

static inline const Eval_Command_State *eval_command_slice_const(const Evaluator_Context *ctx) {
    return ctx ? &ctx->command_state : NULL;
}

static inline Eval_File_State *eval_file_slice(Evaluator_Context *ctx) {
    return ctx ? &ctx->file_state : NULL;
}

static inline const Eval_File_State *eval_file_slice_const(const Evaluator_Context *ctx) {
    return ctx ? &ctx->file_state : NULL;
}

static inline Eval_Process_State *eval_process_slice(Evaluator_Context *ctx) {
    return ctx ? &ctx->process_state : NULL;
}

static inline const Eval_Process_State *eval_process_slice_const(const Evaluator_Context *ctx) {
    return ctx ? &ctx->process_state : NULL;
}

static inline bool eval_mark_oom_if_null(Evaluator_Context *ctx, const void *ptr) {
    if (ptr) return false;
    ctx_oom(ctx);
    return true;
}

#define EVAL_OOM_RETURN_IF_NULL(ctx, ptr, ret_value) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return (ret_value); } while (0)

#define EVAL_OOM_RETURN_VOID_IF_NULL(ctx, ptr) \
    do { if (eval_mark_oom_if_null((ctx), (ptr))) return; } while (0)

#define EVAL_ARR_PUSH(ctx, arena, arr, value) \
    (arena_arr_push((arena), (arr), (value)) ? true : ctx_oom((ctx)))

Eval_Result eval_emit_diag(Evaluator_Context *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Cmake_Event_Origin origin,
                           String_View cause,
                           String_View hint);
Eval_Result eval_emit_diag_with_severity(Evaluator_Context *ctx,
                                         Cmake_Diag_Severity severity,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Cmake_Event_Origin origin,
                                         String_View cause,
                                         String_View hint);

#define EVAL_NODE_ORIGIN_DIAG_RESULT(ctx, node, origin, code, component_lit, cause, hint) \
    eval_emit_diag((ctx),                                                                   \
                   (code),                                                                  \
                   nob_sv_from_cstr((component_lit)),                                       \
                   (node)->as.cmd.name,                                                     \
                   (origin),                                                                \
                   (cause),                                                                 \
                   (hint))

#define EVAL_NODE_ORIGIN_DIAG_RESULT_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    eval_emit_diag_with_severity((ctx),                                                                   \
                                 (severity),                                                              \
                                 (code),                                                                  \
                                 nob_sv_from_cstr((component_lit)),                                       \
                                 (node)->as.cmd.name,                                                     \
                                 (origin),                                                                \
                                 (cause),                                                                 \
                                 (hint))

#define EVAL_NODE_ORIGIN_DIAG_BOOL(ctx, node, origin, code, component_lit, cause, hint) \
    eval_diag_emit_bool((ctx),                                                                  \
                        (code),                                                                 \
                        nob_sv_from_cstr((component_lit)),                                      \
                        (node)->as.cmd.name,                                                    \
                        (origin),                                                               \
                        (cause),                                                                \
                        (hint))

#define EVAL_NODE_ORIGIN_DIAG_BOOL_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    eval_diag_emit_with_severity_bool((ctx),                                                            \
                                      (severity),                                                       \
                                      (code),                                                           \
                                      nob_sv_from_cstr((component_lit)),                                \
                                      (node)->as.cmd.name,                                              \
                                      (origin),                                                         \
                                      (cause),                                                          \
                                      (hint))

#define EVAL_NODE_ORIGIN_DIAG_EMIT(ctx, node, origin, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL((ctx), (node), (origin), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, origin, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL_SEV((ctx), (node), (origin), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_RESULT(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_RESULT((ctx),                                     \
                                 (node),                                    \
                                 eval_origin_from_node((ctx), (node)),      \
                                 (code),                                    \
                                 (component_lit),                           \
                                 (cause),                                   \
                                 (hint))

#define EVAL_NODE_DIAG_RESULT_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_RESULT_SEV((ctx),                                                \
                                     (node),                                               \
                                     eval_origin_from_node((ctx), (node)),                 \
                                     (severity),                                           \
                                     (code),                                               \
                                     (component_lit),                                      \
                                     (cause),                                              \
                                     (hint))

#define EVAL_NODE_DIAG_BOOL(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL((ctx), (node), eval_origin_from_node((ctx), (node)), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_BOOL_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG_BOOL_SEV((ctx), (node), eval_origin_from_node((ctx), (node)), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_EMIT(ctx, node, code, component_lit, cause, hint) \
    EVAL_NODE_DIAG_BOOL((ctx), (node), (code), (component_lit), (cause), (hint))

#define EVAL_NODE_DIAG_EMIT_SEV(ctx, node, severity, code, component_lit, cause, hint) \
    EVAL_NODE_DIAG_BOOL_SEV((ctx), (node), (severity), (code), (component_lit), (cause), (hint))

#define EVAL_DIAG_RESULT(ctx, code, component, command, origin, cause, hint) \
    eval_emit_diag((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_RESULT_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    eval_emit_diag_with_severity((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_BOOL(ctx, code, component, command, origin, cause, hint) \
    eval_diag_emit_bool((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_BOOL_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    eval_diag_emit_with_severity_bool((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_EMIT(ctx, code, component, command, origin, cause, hint) \
    EVAL_DIAG_BOOL((ctx), (code), (component), (command), (origin), (cause), (hint))

#define EVAL_DIAG_EMIT_SEV(ctx, severity, code, component, command, origin, cause, hint) \
    EVAL_DIAG_BOOL_SEV((ctx), (severity), (code), (component), (command), (origin), (cause), (hint))

static inline bool eval_diag_emit_bool(Evaluator_Context *ctx,
                                       Eval_Diag_Code code,
                                       String_View component,
                                       String_View command,
                                       Cmake_Event_Origin origin,
                                       String_View cause,
                                       String_View hint) {
    return !eval_result_is_fatal(eval_emit_diag(ctx, code, component, command, origin, cause, hint));
}

static inline bool eval_diag_emit_with_severity_bool(Evaluator_Context *ctx,
                                                     Cmake_Diag_Severity severity,
                                                     Eval_Diag_Code code,
                                                     String_View component,
                                                     String_View command,
                                                     Cmake_Event_Origin origin,
                                                     String_View cause,
                                                     String_View hint) {
    return !eval_result_is_fatal(eval_emit_diag_with_severity(ctx, severity, code, component, command, origin, cause, hint));
}

static inline void eval_clear_return_state(Evaluator_Context *ctx) {
    if (!ctx) return;
    ctx->return_requested = false;
    ctx->scope_state.return_propagate_vars = NULL;
}

Arena *eval_temp_arena(Evaluator_Context *ctx);
Arena *eval_event_arena(Evaluator_Context *ctx);

static inline bool eval_sv_arr_push_temp(Evaluator_Context *ctx, String_View **arr, String_View sv) {
    if (!ctx || !arr) return false;
    return EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), *arr, sv);
}

String_View sv_copy_to_temp_arena(Evaluator_Context *ctx, String_View sv);
String_View sv_copy_to_event_arena(Evaluator_Context *ctx, String_View sv);
String_View sv_copy_to_arena(Arena *arena, String_View sv);
char *eval_sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv);

static inline bool eval_sv_arr_push_event(Evaluator_Context *ctx, String_View **arr, String_View sv) {
    if (!ctx || !arr) return false;
    String_View copied = sv_copy_to_event_arena(ctx, sv);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, *arr, copied);
}

// ---- rastreio de origem (NOVO) ----
Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node);

// ---- args e expansão (NOVO COMPORTAMENTO) ----
// Retorna a lista expandida e dividida por ';' (alocada na TEMP ARENA)
SV_List eval_resolve_args(Evaluator_Context *ctx, const Args *raw_args);
SV_List eval_resolve_args_literal(Evaluator_Context *ctx, const Args *raw_args);

// ---- diagnostics / events ----
bool eval_emit_event(Evaluator_Context *ctx, Event ev);
bool eval_emit_event_allow_stopped(Evaluator_Context *ctx, Event ev);
static inline bool emit_event(Evaluator_Context *ctx, Event ev) {
    return eval_emit_event(ctx, ev);
}

static inline String_View *eval_sv_list_copy_to_event_arena(Evaluator_Context *ctx, const SV_List *list) {
    if (!ctx || !list || arena_arr_len(*list) == 0) return NULL;
    String_View *items = arena_alloc_array(eval_event_arena(ctx), String_View, arena_arr_len(*list));
    EVAL_OOM_RETURN_IF_NULL(ctx, items, NULL);
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        items[i] = sv_copy_to_event_arena(ctx, (*list)[i]);
        if (eval_should_stop(ctx)) return NULL;
    }
    return items;
}

static inline bool eval_emit_target_prop_set(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             String_View target_name,
                                             String_View key,
                                             String_View value,
                                             Cmake_Target_Property_Op op) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_PROP_SET;
    ev.h.origin = origin;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_declare(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View name,
                                            Cmake_Target_Type target_type,
                                            bool imported,
                                            bool alias,
                                            String_View alias_of) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_DECLARE;
    ev.h.origin = origin;
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.target_type = target_type;
    ev.as.target_declare.imported = imported;
    ev.as.target_declare.alias = alias;
    ev.as.target_declare.alias_of = sv_copy_to_event_arena(ctx, alias_of);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_dependency(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View target_name,
                                               String_View dependency_name) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_ADD_DEPENDENCY;
    ev.h.origin = origin;
    ev.as.target_add_dependency.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_dependency.dependency_name = sv_copy_to_event_arena(ctx, dependency_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_add_source(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View target_name,
                                               String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_ADD_SOURCE;
    ev.h.origin = origin;
    ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_libraries(Evaluator_Context *ctx,
                                                   Event_Origin origin,
                                                   String_View target_name,
                                                   Cmake_Visibility visibility,
                                                   String_View item) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_LIBRARIES;
    ev.h.origin = origin;
    ev.as.target_link_libraries.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_libraries.visibility = visibility;
    ev.as.target_link_libraries.item = sv_copy_to_event_arena(ctx, item);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_options(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View target_name,
                                                 Cmake_Visibility visibility,
                                                 String_View item,
                                                 bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_OPTIONS;
    ev.h.origin = origin;
    ev.as.target_link_options.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_options.visibility = visibility;
    ev.as.target_link_options.item = sv_copy_to_event_arena(ctx, item);
    ev.as.target_link_options.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_link_directories(Evaluator_Context *ctx,
                                                     Event_Origin origin,
                                                     String_View target_name,
                                                     Cmake_Visibility visibility,
                                                     String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_LINK_DIRECTORIES;
    ev.h.origin = origin;
    ev.as.target_link_directories.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_link_directories.visibility = visibility;
    ev.as.target_link_directories.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_include_directories(Evaluator_Context *ctx,
                                                        Event_Origin origin,
                                                        String_View target_name,
                                                        Cmake_Visibility visibility,
                                                        String_View path,
                                                        bool is_system,
                                                        bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_INCLUDE_DIRECTORIES;
    ev.h.origin = origin;
    ev.as.target_include_directories.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_include_directories.visibility = visibility;
    ev.as.target_include_directories.path = sv_copy_to_event_arena(ctx, path);
    ev.as.target_include_directories.is_system = is_system;
    ev.as.target_include_directories.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_compile_definitions(Evaluator_Context *ctx,
                                                        Event_Origin origin,
                                                        String_View target_name,
                                                        Cmake_Visibility visibility,
                                                        String_View item) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_COMPILE_DEFINITIONS;
    ev.h.origin = origin;
    ev.as.target_compile_definitions.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_compile_definitions.visibility = visibility;
    ev.as.target_compile_definitions.item = sv_copy_to_event_arena(ctx, item);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_compile_options(Evaluator_Context *ctx,
                                                    Event_Origin origin,
                                                    String_View target_name,
                                                    Cmake_Visibility visibility,
                                                    String_View item,
                                                    bool is_before) {
    Event ev = {0};
    ev.h.kind = EVENT_TARGET_COMPILE_OPTIONS;
    ev.h.origin = origin;
    ev.as.target_compile_options.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_compile_options.visibility = visibility;
    ev.as.target_compile_options.item = sv_copy_to_event_arena(ctx, item);
    ev.as.target_compile_options.is_before = is_before;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_set_current(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             String_View key,
                                             String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_SET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.var_set.target_kind = EVENT_VAR_TARGET_CURRENT;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_unset_current(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View key) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_UNSET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_unset.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_unset.target_kind = EVENT_VAR_TARGET_CURRENT;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_set_cache(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           String_View key,
                                           String_View value) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_SET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.var_set.target_kind = EVENT_VAR_TARGET_CACHE;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_unset_cache(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             String_View key) {
    Event ev = {0};
    ev.h.kind = EVENT_VAR_UNSET;
    ev.h.version = 2;
    ev.h.origin = origin;
    ev.as.var_unset.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_unset.target_kind = EVENT_VAR_TARGET_CACHE;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_test_enable(Evaluator_Context *ctx,
                                         Event_Origin origin) {
    Event ev = {0};
    ev.h.kind = EVENT_TEST_ENABLE;
    ev.h.origin = origin;
    ev.as.test_enable.enabled = true;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_test_add(Evaluator_Context *ctx,
                                      Event_Origin origin,
                                      String_View name,
                                      String_View command,
                                      String_View working_dir,
                                      bool command_expand_lists) {
    Event ev = {0};
    ev.h.kind = EVENT_TEST_ADD;
    ev.h.origin = origin;
    ev.as.test_add.name = sv_copy_to_event_arena(ctx, name);
    ev.as.test_add.command = sv_copy_to_event_arena(ctx, command);
    ev.as.test_add.working_dir = sv_copy_to_event_arena(ctx, working_dir);
    ev.as.test_add.command_expand_lists = command_expand_lists;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_install_rule_add(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              Cmake_Install_Rule_Type rule_type,
                                              String_View item,
                                              String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_INSTALL_RULE_ADD;
    ev.h.origin = origin;
    ev.as.install_rule_add.rule_type = rule_type;
    ev.as.install_rule_add.item = sv_copy_to_event_arena(ctx, item);
    ev.as.install_rule_add.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_install_type(Evaluator_Context *ctx,
                                                    Event_Origin origin,
                                                    String_View name,
                                                    String_View display_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_INSTALL_TYPE;
    ev.h.origin = origin;
    ev.as.cpack_add_install_type.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_install_type.display_name = sv_copy_to_event_arena(ctx, display_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_component_group(Evaluator_Context *ctx,
                                                       Event_Origin origin,
                                                       String_View name,
                                                       String_View display_name,
                                                       String_View description,
                                                       String_View parent_group,
                                                       bool expanded,
                                                       bool bold_title) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_COMPONENT_GROUP;
    ev.h.origin = origin;
    ev.as.cpack_add_component_group.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component_group.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component_group.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component_group.parent_group = sv_copy_to_event_arena(ctx, parent_group);
    ev.as.cpack_add_component_group.expanded = expanded;
    ev.as.cpack_add_component_group.bold_title = bold_title;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cpack_add_component(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View name,
                                                 String_View display_name,
                                                 String_View description,
                                                 String_View group,
                                                 String_View depends,
                                                 String_View install_types,
                                                 String_View archive_file,
                                                 String_View plist,
                                                 bool required,
                                                 bool hidden,
                                                 bool disabled,
                                                 bool downloaded) {
    Event ev = {0};
    ev.h.kind = EVENT_CPACK_ADD_COMPONENT;
    ev.h.origin = origin;
    ev.as.cpack_add_component.name = sv_copy_to_event_arena(ctx, name);
    ev.as.cpack_add_component.display_name = sv_copy_to_event_arena(ctx, display_name);
    ev.as.cpack_add_component.description = sv_copy_to_event_arena(ctx, description);
    ev.as.cpack_add_component.group = sv_copy_to_event_arena(ctx, group);
    ev.as.cpack_add_component.depends = sv_copy_to_event_arena(ctx, depends);
    ev.as.cpack_add_component.install_types = sv_copy_to_event_arena(ctx, install_types);
    ev.as.cpack_add_component.archive_file = sv_copy_to_event_arena(ctx, archive_file);
    ev.as.cpack_add_component.plist = sv_copy_to_event_arena(ctx, plist);
    ev.as.cpack_add_component.required = required;
    ev.as.cpack_add_component.hidden = hidden;
    ev.as.cpack_add_component.disabled = disabled;
    ev.as.cpack_add_component.downloaded = downloaded;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_package_find_result(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View package_name,
                                                 String_View mode,
                                                 String_View found_path,
                                                 bool found,
                                                 bool required,
                                                 bool quiet) {
    Event ev = {0};
    ev.h.kind = EVENT_PACKAGE_FIND_RESULT;
    ev.h.origin = origin;
    ev.as.package_find_result.package_name = sv_copy_to_event_arena(ctx, package_name);
    ev.as.package_find_result.mode = sv_copy_to_event_arena(ctx, mode);
    ev.as.package_find_result.found_path = sv_copy_to_event_arena(ctx, found_path);
    ev.as.package_find_result.found = found;
    ev.as.package_find_result.required = required;
    ev.as.package_find_result.quiet = quiet;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_project_declare(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             String_View name,
                                             String_View version,
                                             String_View description,
                                             String_View homepage_url,
                                             String_View languages) {
    Event ev = {0};
    ev.h.kind = EVENT_PROJECT_DECLARE;
    ev.h.origin = origin;
    ev.as.project_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.project_declare.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_declare.description = sv_copy_to_event_arena(ctx, description);
    ev.as.project_declare.homepage_url = sv_copy_to_event_arena(ctx, homepage_url);
    ev.as.project_declare.languages = sv_copy_to_event_arena(ctx, languages);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_project_minimum_required(Evaluator_Context *ctx,
                                                      Event_Origin origin,
                                                      String_View version,
                                                      bool fatal_if_too_old) {
    Event ev = {0};
    ev.h.kind = EVENT_PROJECT_MINIMUM_REQUIRED;
    ev.h.origin = origin;
    ev.as.project_minimum_required.version = sv_copy_to_event_arena(ctx, version);
    ev.as.project_minimum_required.fatal_if_too_old = fatal_if_too_old;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_include_begin(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           String_View path,
                                           bool no_policy_scope) {
    Event ev = {0};
    ev.h.kind = EVENT_INCLUDE_BEGIN;
    ev.h.origin = origin;
    ev.as.include_begin.path = sv_copy_to_event_arena(ctx, path);
    ev.as.include_begin.no_policy_scope = no_policy_scope;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_include_end(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View path,
                                         bool success) {
    Event ev = {0};
    ev.h.kind = EVENT_INCLUDE_END;
    ev.h.origin = origin;
    ev.as.include_end.path = sv_copy_to_event_arena(ctx, path);
    ev.as.include_end.success = success;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_add_subdirectory_begin(Evaluator_Context *ctx,
                                                    Event_Origin origin,
                                                    String_View source_dir,
                                                    String_View binary_dir,
                                                    bool exclude_from_all,
                                                    bool system) {
    Event ev = {0};
    ev.h.kind = EVENT_ADD_SUBDIRECTORY_BEGIN;
    ev.h.origin = origin;
    ev.as.add_subdirectory_begin.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.add_subdirectory_begin.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    ev.as.add_subdirectory_begin.exclude_from_all = exclude_from_all;
    ev.as.add_subdirectory_begin.system = system;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_add_subdirectory_end(Evaluator_Context *ctx,
                                                  Event_Origin origin,
                                                  String_View source_dir,
                                                  String_View binary_dir,
                                                  bool success) {
    Event ev = {0};
    ev.h.kind = EVENT_ADD_SUBDIRECTORY_END;
    ev.h.origin = origin;
    ev.as.add_subdirectory_end.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.add_subdirectory_end.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    ev.as.add_subdirectory_end.success = success;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_directory_property_mutate(Evaluator_Context *ctx,
                                                       Event_Origin origin,
                                                       String_View property_name,
                                                       Event_Property_Mutate_Op op,
                                                       uint32_t modifier_flags,
                                                       String_View *items,
                                                       size_t item_count) {
    Event ev = {0};
    ev.h.kind = EVENT_DIRECTORY_PROPERTY_MUTATE;
    ev.h.origin = origin;
    ev.as.directory_property_mutate.property_name = property_name;
    ev.as.directory_property_mutate.op = op;
    ev.as.directory_property_mutate.modifier_flags = modifier_flags;
    ev.as.directory_property_mutate.items = items;
    ev.as.directory_property_mutate.item_count = item_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_global_property_mutate(Evaluator_Context *ctx,
                                                    Event_Origin origin,
                                                    String_View property_name,
                                                    Event_Property_Mutate_Op op,
                                                    uint32_t modifier_flags,
                                                    String_View *items,
                                                    size_t item_count) {
    Event ev = {0};
    ev.h.kind = EVENT_GLOBAL_PROPERTY_MUTATE;
    ev.h.origin = origin;
    ev.as.global_property_mutate.property_name = property_name;
    ev.as.global_property_mutate.op = op;
    ev.as.global_property_mutate.modifier_flags = modifier_flags;
    ev.as.global_property_mutate.items = items;
    ev.as.global_property_mutate.item_count = item_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_dir_push(Evaluator_Context *ctx,
                                      Event_Origin origin,
                                      String_View source_dir,
                                      String_View binary_dir) {
    Event ev = {0};
    ev.h.kind = EVENT_DIR_PUSH;
    ev.h.origin = origin;
    ev.as.dir_push.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_push.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_dir_pop(Evaluator_Context *ctx,
                                     Event_Origin origin,
                                     String_View source_dir,
                                     String_View binary_dir) {
    Event ev = {0};
    ev.h.kind = EVENT_DIR_POP;
    ev.h.origin = origin;
    ev.as.dir_pop.source_dir = sv_copy_to_event_arena(ctx, source_dir);
    ev.as.dir_pop.binary_dir = sv_copy_to_event_arena(ctx, binary_dir);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_command_begin(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           String_View command_name,
                                           Event_Command_Dispatch_Kind dispatch_kind,
                                           uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_BEGIN;
    ev.h.origin = origin;
    ev.as.command_begin.command_name = command_name;
    ev.as.command_begin.dispatch_kind = dispatch_kind;
    ev.as.command_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_command_end(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View command_name,
                                         Event_Command_Dispatch_Kind dispatch_kind,
                                         uint32_t argc,
                                         Event_Command_Status status) {
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_END;
    ev.h.origin = origin;
    ev.as.command_end.command_name = command_name;
    ev.as.command_end.dispatch_kind = dispatch_kind;
    ev.as.command_end.argc = argc;
    ev.as.command_end.status = status;
    return eval_emit_event_allow_stopped(ctx, ev);
}
static inline bool eval_emit_command_call(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View command_name) {
    return eval_emit_command_begin(ctx,
                                   origin,
                                   command_name,
                                   EVENT_COMMAND_DISPATCH_BUILTIN,
                                   0);
}
static inline bool eval_emit_cmake_language_call(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_CALL;
    ev.h.origin = origin;
    ev.as.cmake_language_call.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cmake_language_eval(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View code) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_EVAL;
    ev.h.origin = origin;
    ev.as.cmake_language_eval.code = sv_copy_to_event_arena(ctx, code);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_cmake_language_defer_queue(Evaluator_Context *ctx,
                                                        Event_Origin origin,
                                                        String_View defer_id,
                                                        String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_CMAKE_LANGUAGE_DEFER_QUEUE;
    ev.h.origin = origin;
    ev.as.cmake_language_defer_queue.defer_id = sv_copy_to_event_arena(ctx, defer_id);
    ev.as.cmake_language_defer_queue.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_write_file(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_WRITE_FILE;
    ev.h.origin = origin;
    ev.as.fs_write_file.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_append_file(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_APPEND_FILE;
    ev.h.origin = origin;
    ev.as.fs_append_file.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_read_file(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View path,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_READ_FILE;
    ev.h.origin = origin;
    ev.as.fs_read_file.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_read_file.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_glob(Evaluator_Context *ctx,
                                     Event_Origin origin,
                                     String_View out_var,
                                     String_View base_dir,
                                     bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_GLOB;
    ev.h.origin = origin;
    ev.as.fs_glob.out_var = sv_copy_to_event_arena(ctx, out_var);
    ev.as.fs_glob.base_dir = sv_copy_to_event_arena(ctx, base_dir);
    ev.as.fs_glob.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_mkdir(Evaluator_Context *ctx,
                                      Event_Origin origin,
                                      String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_MKDIR;
    ev.h.origin = origin;
    ev.as.fs_mkdir.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_remove(Evaluator_Context *ctx,
                                       Event_Origin origin,
                                       String_View path,
                                       bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_REMOVE;
    ev.h.origin = origin;
    ev.as.fs_remove.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_remove.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_copy(Evaluator_Context *ctx,
                                     Event_Origin origin,
                                     String_View source,
                                     String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_COPY;
    ev.h.origin = origin;
    ev.as.fs_copy.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_copy.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_rename(Evaluator_Context *ctx,
                                       Event_Origin origin,
                                       String_View source,
                                       String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_RENAME;
    ev.h.origin = origin;
    ev.as.fs_rename.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_rename.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_create_link(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View source,
                                            String_View destination,
                                            bool symbolic) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_CREATE_LINK;
    ev.h.origin = origin;
    ev.as.fs_create_link.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_create_link.destination = sv_copy_to_event_arena(ctx, destination);
    ev.as.fs_create_link.symbolic = symbolic;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_chmod(Evaluator_Context *ctx,
                                      Event_Origin origin,
                                      String_View path,
                                      bool recursive) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_CHMOD;
    ev.h.origin = origin;
    ev.as.fs_chmod.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_chmod.recursive = recursive;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_archive_create(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View path) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_ARCHIVE_CREATE;
    ev.h.origin = origin;
    ev.as.fs_archive_create.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_archive_extract(Evaluator_Context *ctx,
                                                Event_Origin origin,
                                                String_View path,
                                                String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_ARCHIVE_EXTRACT;
    ev.h.origin = origin;
    ev.as.fs_archive_extract.path = sv_copy_to_event_arena(ctx, path);
    ev.as.fs_archive_extract.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_transfer_download(Evaluator_Context *ctx,
                                                  Event_Origin origin,
                                                  String_View source,
                                                  String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_TRANSFER_DOWNLOAD;
    ev.h.origin = origin;
    ev.as.fs_transfer_download.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_transfer_download.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_fs_transfer_upload(Evaluator_Context *ctx,
                                                Event_Origin origin,
                                                String_View source,
                                                String_View destination) {
    Event ev = {0};
    ev.h.kind = EVENT_FS_TRANSFER_UPLOAD;
    ev.h.origin = origin;
    ev.as.fs_transfer_upload.source = sv_copy_to_event_arena(ctx, source);
    ev.as.fs_transfer_upload.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_proc_exec_request(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View command,
                                               String_View working_directory) {
    Event ev = {0};
    ev.h.kind = EVENT_PROC_EXEC_REQUEST;
    ev.h.origin = origin;
    ev.as.proc_exec_request.command = sv_copy_to_event_arena(ctx, command);
    ev.as.proc_exec_request.working_directory = sv_copy_to_event_arena(ctx, working_directory);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_proc_exec_result(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              String_View command,
                                              String_View result_code,
                                              String_View stdout_text,
                                              String_View stderr_text,
                                              bool had_error) {
    Event ev = {0};
    ev.h.kind = EVENT_PROC_EXEC_RESULT;
    ev.h.origin = origin;
    ev.as.proc_exec_result.command = sv_copy_to_event_arena(ctx, command);
    ev.as.proc_exec_result.result_code = sv_copy_to_event_arena(ctx, result_code);
    ev.as.proc_exec_result.stdout_text = sv_copy_to_event_arena(ctx, stdout_text);
    ev.as.proc_exec_result.stderr_text = sv_copy_to_event_arena(ctx, stderr_text);
    ev.as.proc_exec_result.had_error = had_error;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_if_eval(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          bool result) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_IF_EVAL;
    ev.h.origin = origin;
    ev.as.flow_if_eval.result = result;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_branch_taken(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View branch_kind) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BRANCH_TAKEN;
    ev.h.origin = origin;
    ev.as.flow_branch_taken.branch_kind = sv_copy_to_event_arena(ctx, branch_kind);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_loop_begin(Evaluator_Context *ctx,
                                             Event_Origin origin,
                                             String_View loop_kind) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_LOOP_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_loop_begin.loop_kind = sv_copy_to_event_arena(ctx, loop_kind);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_loop_end(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           String_View loop_kind,
                                           uint32_t iterations) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_LOOP_END;
    ev.h.origin = origin;
    ev.as.flow_loop_end.loop_kind = sv_copy_to_event_arena(ctx, loop_kind);
    ev.as.flow_loop_end.iterations = iterations;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_break(Evaluator_Context *ctx,
                                        Event_Origin origin,
                                        uint32_t loop_depth) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BREAK;
    ev.h.origin = origin;
    ev.as.flow_break.loop_depth = loop_depth;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_continue(Evaluator_Context *ctx,
                                           Event_Origin origin,
                                           uint32_t loop_depth) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_CONTINUE;
    ev.h.origin = origin;
    ev.as.flow_continue.loop_depth = loop_depth;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_defer_queue(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              String_View defer_id,
                                              String_View command_name) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_DEFER_QUEUE;
    ev.h.origin = origin;
    ev.as.flow_defer_queue.defer_id = sv_copy_to_event_arena(ctx, defer_id);
    ev.as.flow_defer_queue.command_name = sv_copy_to_event_arena(ctx, command_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_defer_flush(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              uint32_t call_count) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_DEFER_FLUSH;
    ev.h.origin = origin;
    ev.as.flow_defer_flush.call_count = call_count;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_block_begin(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              bool variable_scope_pushed,
                                              bool policy_scope_pushed,
                                              bool has_propagate_vars) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BLOCK_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_block_begin.variable_scope_pushed = variable_scope_pushed;
    ev.as.flow_block_begin.policy_scope_pushed = policy_scope_pushed;
    ev.as.flow_block_begin.has_propagate_vars = has_propagate_vars;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_block_end(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            bool propagate_on_return,
                                            bool had_propagate_vars) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_BLOCK_END;
    ev.h.origin = origin;
    ev.as.flow_block_end.propagate_on_return = propagate_on_return;
    ev.as.flow_block_end.had_propagate_vars = had_propagate_vars;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_function_begin(Evaluator_Context *ctx,
                                                 Event_Origin origin,
                                                 String_View name,
                                                 uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_FUNCTION_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_function_begin.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_function_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_function_end(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View name,
                                               bool returned) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_FUNCTION_END;
    ev.h.origin = origin;
    ev.as.flow_function_end.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_function_end.returned = returned;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_macro_begin(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              String_View name,
                                              uint32_t argc) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_MACRO_BEGIN;
    ev.h.origin = origin;
    ev.as.flow_macro_begin.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_macro_begin.argc = argc;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_flow_macro_end(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View name,
                                            bool returned) {
    Event ev = {0};
    ev.h.kind = EVENT_FLOW_MACRO_END;
    ev.h.origin = origin;
    ev.as.flow_macro_end.name = sv_copy_to_event_arena(ctx, name);
    ev.as.flow_macro_end.returned = returned;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_replace(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_REPLACE;
    ev.h.origin = origin;
    ev.as.string_replace.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_configure(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_CONFIGURE;
    ev.h.origin = origin;
    ev.as.string_configure.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_regex(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View mode,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_REGEX;
    ev.h.origin = origin;
    ev.as.string_regex.mode = sv_copy_to_event_arena(ctx, mode);
    ev.as.string_regex.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_hash(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View algorithm,
                                         String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_HASH;
    ev.h.origin = origin;
    ev.as.string_hash.algorithm = sv_copy_to_event_arena(ctx, algorithm);
    ev.as.string_hash.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_string_timestamp(Evaluator_Context *ctx,
                                              Event_Origin origin,
                                              String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_STRING_TIMESTAMP;
    ev.h.origin = origin;
    ev.as.string_timestamp.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_append(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_APPEND;
    ev.h.origin = origin;
    ev.as.list_append.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_prepend(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_PREPEND;
    ev.h.origin = origin;
    ev.as.list_prepend.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_insert(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_INSERT;
    ev.h.origin = origin;
    ev.as.list_insert.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_remove(Evaluator_Context *ctx,
                                         Event_Origin origin,
                                         String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_REMOVE;
    ev.h.origin = origin;
    ev.as.list_remove.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_transform(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_TRANSFORM;
    ev.h.origin = origin;
    ev.as.list_transform.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_list_sort(Evaluator_Context *ctx,
                                       Event_Origin origin,
                                       String_View list_var) {
    Event ev = {0};
    ev.h.kind = EVENT_LIST_SORT;
    ev.h.origin = origin;
    ev.as.list_sort.list_var = sv_copy_to_event_arena(ctx, list_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_math_expr(Evaluator_Context *ctx,
                                       Event_Origin origin,
                                       String_View out_var,
                                       String_View format) {
    Event ev = {0};
    ev.h.kind = EVENT_MATH_EXPR;
    ev.h.origin = origin;
    ev.as.math_expr.out_var = sv_copy_to_event_arena(ctx, out_var);
    ev.as.math_expr.format = sv_copy_to_event_arena(ctx, format);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_normalize(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_NORMALIZE;
    ev.h.origin = origin;
    ev.as.path_normalize.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_compare(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_COMPARE;
    ev.h.origin = origin;
    ev.as.path_compare.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_path_convert(Evaluator_Context *ctx,
                                          Event_Origin origin,
                                          String_View out_var) {
    Event ev = {0};
    ev.h.kind = EVENT_PATH_CONVERT;
    ev.h.origin = origin;
    ev.as.path_convert.out_var = sv_copy_to_event_arena(ctx, out_var);
    return emit_event(ctx, ev);
}
Eval_Result eval_emit_diag(Evaluator_Context *ctx,
                           Eval_Diag_Code code,
                           String_View component,
                           String_View command,
                           Event_Origin origin,
                           String_View cause,
                           String_View hint);
Eval_Result eval_emit_diag_with_severity(Evaluator_Context *ctx,
                                         Event_Diag_Severity sev,
                                         Eval_Diag_Code code,
                                         String_View component,
                                         String_View command,
                                         Event_Origin origin,
                                         String_View cause,
                                         String_View hint);
String_View eval_diag_code_to_sv(Eval_Diag_Code code);
Eval_Error_Class eval_diag_error_class(Eval_Diag_Code code);
Cmake_Diag_Severity eval_diag_default_severity(Eval_Diag_Code code);
bool eval_diag_counts_as_unsupported(Eval_Diag_Code code);
String_View eval_error_class_to_sv(Eval_Error_Class cls);
void eval_report_reset(Evaluator_Context *ctx);
void eval_report_record_diag(Evaluator_Context *ctx,
                             Event_Diag_Severity input_sev,
                             Event_Diag_Severity effective_sev,
                             Eval_Diag_Code code);
void eval_report_finalize(Evaluator_Context *ctx);
bool eval_command_caps_lookup(const Evaluator_Context *ctx, String_View name, Command_Capability *out_capability);
bool eval_append_configure_log(Evaluator_Context *ctx, const Node *node, String_View msg);

// ---- vars ----
String_View eval_var_get_visible(Evaluator_Context *ctx, String_View key);
static inline String_View eval_var_get(Evaluator_Context *ctx, String_View key) {
    return eval_var_get_visible(ctx, key);
}
bool eval_var_defined_visible(Evaluator_Context *ctx, String_View key);
bool eval_var_set_current(Evaluator_Context *ctx, String_View key, String_View value);
bool eval_var_unset_current(Evaluator_Context *ctx, String_View key);
bool eval_var_defined_current(Evaluator_Context *ctx, String_View key);
bool eval_var_collect_visible_names(Evaluator_Context *ctx, SV_List *out_names);
bool eval_cache_defined(Evaluator_Context *ctx, String_View key);
bool eval_cache_set(Evaluator_Context *ctx,
                    String_View key,
                    String_View value,
                    String_View type,
                    String_View doc);

static inline String_View eval_current_source_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR));
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_binary_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR));
    if (v.count == 0 && ctx) v = ctx->binary_dir;
    return v;
}

static inline String_View eval_current_list_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR));
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_list_file(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE));
    if (v.count == 0 && ctx && ctx->current_file) v = nob_sv_from_cstr(ctx->current_file);
    return v;
}

// ---- targets ----
bool eval_target_known(Evaluator_Context *ctx, String_View name);
bool eval_target_register(Evaluator_Context *ctx, String_View name);
bool eval_target_declared_dir(Evaluator_Context *ctx, String_View name, String_View *out_dir);
bool eval_target_alias_known(Evaluator_Context *ctx, String_View name);
bool eval_target_alias_register(Evaluator_Context *ctx, String_View name);
bool eval_property_define(Evaluator_Context *ctx, const Eval_Property_Definition *definition);
bool eval_property_is_defined(Evaluator_Context *ctx, String_View scope_upper, String_View property_name);
bool eval_target_apply_defined_initializers(Evaluator_Context *ctx, Event_Origin origin, String_View target_name);
bool eval_property_write(Evaluator_Context *ctx,
                         Event_Origin origin,
                         String_View scope_upper,
                         String_View object_id,
                         String_View property_name,
                         String_View value,
                         Cmake_Target_Property_Op op,
                         bool emit_var_event);
bool eval_property_query_mode_parse(Evaluator_Context *ctx,
                                    const Node *node,
                                    Event_Origin origin,
                                    String_View token,
                                    Eval_Property_Query_Mode *out_mode);
bool eval_property_query(Evaluator_Context *ctx,
                         const Node *node,
                         Event_Origin origin,
                         String_View out_var,
                         String_View scope_upper,
                         String_View object_id,
                         String_View validation_object,
                         String_View property_name,
                         Eval_Property_Query_Mode mode,
                         String_View inherit_directory,
                         bool validate_object,
                         bool missing_as_notfound);
bool eval_property_query_cmake(Evaluator_Context *ctx,
                               const Node *node,
                               Event_Origin origin,
                               String_View out_var,
                               String_View property_name);
bool eval_property_store_set_key(Evaluator_Context *ctx, String_View store_key, String_View value);
bool eval_property_store_get_key(Evaluator_Context *ctx,
                                 String_View store_key,
                                 String_View *out_value,
                                 bool *out_set);

// ---- native commands ----
Eval_Native_Command *eval_native_cmd_find(Evaluator_Context *ctx, String_View name);
const Eval_Native_Command *eval_native_cmd_find_const(const Evaluator_Context *ctx, String_View name);
bool eval_native_cmd_register_internal(Evaluator_Context *ctx,
                                       const Evaluator_Native_Command_Def *def,
                                       bool is_builtin,
                                       bool allow_during_run);
bool eval_native_cmd_unregister_internal(Evaluator_Context *ctx,
                                         String_View name,
                                         bool allow_builtin_remove,
                                         bool allow_during_run);

// ---- user commands ----
bool eval_user_cmd_register(Evaluator_Context *ctx, const Node *node);
bool eval_user_cmd_invoke(Evaluator_Context *ctx, String_View name, const SV_List *args, Cmake_Event_Origin origin);
User_Command *eval_user_cmd_find(Evaluator_Context *ctx, String_View name);

// ---- utilitários compartilhados ----
bool eval_sv_key_eq(String_View a, String_View b);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
String_View eval_normalize_compile_definition_item(String_View item);
String_View eval_current_source_dir_for_paths(Evaluator_Context *ctx);
String_View eval_property_upper_name_temp(Evaluator_Context *ctx, String_View name);
bool eval_property_scope_upper_temp(Evaluator_Context *ctx, String_View raw_scope, String_View *out_scope_upper);
String_View eval_property_scoped_object_id_temp(Evaluator_Context *ctx,
                                                const char *prefix,
                                                String_View scope_object,
                                                String_View item_object);
const Eval_Property_Definition *eval_property_definition_find(Evaluator_Context *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name);
bool eval_test_exists_in_directory_scope(Evaluator_Context *ctx, String_View test_name, String_View scope_dir);
bool eval_semver_parse_strict(String_View version_token, Eval_Semver *out_version);
int eval_semver_compare(const Eval_Semver *lhs, const Eval_Semver *rhs);
String_View eval_detect_host_system_name(void);
String_View eval_detect_host_processor(void);
bool eval_host_hostname_temp(Evaluator_Context *ctx, String_View *out_hostname);
bool eval_host_memory_info(Eval_Host_Memory_Info *out_info);
bool eval_host_logical_cores(size_t *out_count);
String_View eval_host_os_release_temp(Evaluator_Context *ctx);
String_View eval_host_os_version_temp(Evaluator_Context *ctx);
String_View eval_sv_join_semi_temp(Evaluator_Context *ctx, String_View *items, size_t count);
bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out);
bool eval_split_shell_like_temp(Evaluator_Context *ctx, String_View input, SV_List *out);
bool eval_split_command_line_temp(Evaluator_Context *ctx, Eval_Cmdline_Mode mode, String_View input, SV_List *out_tokens);
bool eval_process_run_capture(Evaluator_Context *ctx, const Eval_Process_Run_Request *req, Eval_Process_Run_Result *out);
bool eval_process_run_nob_cmd_capture(Evaluator_Context *ctx,
                                      const Nob_Cmd *cmd,
                                      String_View working_directory,
                                      String_View stdin_data,
                                      Eval_Process_Run_Result *out);
bool eval_process_env_set(Evaluator_Context *ctx, String_View name, String_View value);
bool eval_process_env_unset(Evaluator_Context *ctx, String_View name);
String_View eval_process_cwd_temp(Evaluator_Context *ctx);
bool eval_sv_is_abs_path(String_View p);
String_View eval_sv_path_join(Arena *arena, String_View a, String_View b);
String_View eval_sv_path_normalize_temp(Evaluator_Context *ctx, String_View input);
bool eval_list_dir_sources_sorted_temp(Evaluator_Context *ctx, String_View dir, SV_List *out_sources);
bool eval_mkdirs_for_parent(Evaluator_Context *ctx, String_View path);
bool eval_write_text_file(Evaluator_Context *ctx, String_View path, String_View contents, bool append);
bool eval_ctest_publish_metadata(Evaluator_Context *ctx, String_View command_name, const SV_List *argv, String_View status);
bool eval_legacy_publish_args(Evaluator_Context *ctx, String_View command_name, const SV_List *argv);
String_View eval_path_resolve_for_cmake_arg(Evaluator_Context *ctx,
                                            String_View raw_path,
                                            String_View base_dir,
                                            bool preserve_generator_expressions);
const char *eval_getenv_temp(Evaluator_Context *ctx, const char *name);
bool eval_has_env(Evaluator_Context *ctx, const char *name);
bool eval_real_path_resolve_temp(Evaluator_Context *ctx,
                                 String_View path,
                                 bool cmp0152_new,
                                 String_View *out_path);

// ---- macro argument substitution (textual) ----
bool eval_macro_frame_push(Evaluator_Context *ctx);
void eval_macro_frame_pop(Evaluator_Context *ctx);
bool eval_macro_bind_set(Evaluator_Context *ctx, String_View key, String_View value);
bool eval_macro_bind_get(Evaluator_Context *ctx, String_View key, String_View *out_value);

// ---- Gerenciamento de Escopo ----
bool eval_scope_push(Evaluator_Context *ctx);
void eval_scope_pop(Evaluator_Context *ctx);

bool eval_defer_push_directory(Evaluator_Context *ctx, String_View source_dir, String_View binary_dir);
bool eval_defer_pop_directory(Evaluator_Context *ctx);
bool eval_defer_flush_current_directory(Evaluator_Context *ctx);

bool eval_policy_is_id(String_View policy_id);
bool eval_policy_is_known(String_View policy_id);
bool eval_policy_get_intro_version(String_View policy_id, int *major, int *minor, int *patch);
bool eval_policy_push(Evaluator_Context *ctx);
bool eval_policy_pop(Evaluator_Context *ctx);
bool eval_policy_set_status(Evaluator_Context *ctx, String_View policy_id, Eval_Policy_Status status);
bool eval_policy_set(Evaluator_Context *ctx, String_View policy_id, String_View value);
String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id);

// ---- Execução Externa (Subdiretórios e Includes) ----
// Retorna false em caso de OOM ou erro fatal.
Eval_Result eval_execute_file(Evaluator_Context *ctx, String_View file_path, bool is_add_subdirectory, String_View explicit_bin_dir);
Eval_Result eval_run_ast_inline(Evaluator_Context *ctx, Ast_Root ast);

#ifdef __cplusplus
}
#endif

#endif // EVALUATOR_INTERNAL_H_
