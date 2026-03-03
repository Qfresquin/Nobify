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
    // Invariant: visible_scope_depth <= arena_arr_len(scopes)
    size_t visible_scope_depth;
    Eval_Cache_Entry *cache_entries;

    SV_List known_targets; 
    SV_List alias_targets;
    SV_List message_check_stack;
    User_Command_List user_commands;
    Macro_Frame_Stack macro_frames;
    Block_Frame_Stack block_frames;
    Eval_File_Lock_List file_locks;
    Eval_File_Generate_Job_List file_generate_jobs;
    Eval_Property_Definition_List property_definitions;
    Eval_Deferred_Dir_Frame_Stack deferred_dirs;
    SV_List active_find_packages;
    SV_List watched_variables;
    SV_List watched_variable_commands;
    size_t next_deferred_call_id;
    Eval_Policy_Level *policy_levels;
    // Invariant: visible_policy_depth <= arena_arr_len(policy_levels)
    size_t visible_policy_depth;
    bool cpack_component_module_loaded;
    size_t file_eval_depth;
    size_t function_eval_depth;

    size_t loop_depth;
    bool break_requested;
    bool continue_requested;
    bool return_requested;
    Eval_Return_Context return_context;
    String_View *return_propagate_vars;
    Eval_Compat_Profile compat_profile;
    Eval_Unsupported_Policy unsupported_policy;
    size_t error_budget;
    Eval_Run_Report run_report;

    bool oom;
    bool stop_requested;
    bool in_variable_watch_notification;
};

// ---- controle e memória ----
bool eval_should_stop(Evaluator_Context *ctx);
void eval_request_stop(Evaluator_Context *ctx);
void eval_request_stop_on_error(Evaluator_Context *ctx);
bool eval_continue_on_error(Evaluator_Context *ctx);
void eval_refresh_runtime_compat(Evaluator_Context *ctx);
bool eval_compat_set_profile(Evaluator_Context *ctx, Eval_Compat_Profile profile);
Cmake_Diag_Severity eval_compat_effective_severity(const Evaluator_Context *ctx, Cmake_Diag_Severity sev);
bool eval_compat_decide_on_diag(Evaluator_Context *ctx, Cmake_Diag_Severity effective_sev);
String_View eval_compat_profile_to_sv(Eval_Compat_Profile profile);
bool ctx_oom(Evaluator_Context *ctx);
bool eval_sv_eq_ci_lit(String_View a, const char *lit);
String_View eval_policy_get_effective(Evaluator_Context *ctx, String_View policy_id);

static inline size_t eval_scope_visible_depth(const Evaluator_Context *ctx) {
    return ctx ? ctx->visible_scope_depth : 0;
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

static inline bool eval_scope_enter_parent(Evaluator_Context *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth <= 1) return false;
    *saved_depth = depth;
    ctx->visible_scope_depth = depth - 1;
    return true;
}

static inline bool eval_scope_enter_global(Evaluator_Context *ctx, size_t *saved_depth) {
    if (!ctx || !saved_depth) return false;
    size_t depth = eval_scope_visible_depth(ctx);
    if (depth == 0) return false;
    *saved_depth = depth;
    ctx->visible_scope_depth = 1;
    return true;
}

static inline void eval_scope_leave(Evaluator_Context *ctx, size_t saved_depth) {
    if (ctx) ctx->visible_scope_depth = saved_depth;
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

#define EVAL_NODE_ORIGIN_DIAG(ctx, node, origin, severity, component_lit, cause, hint) \
    eval_emit_diag((ctx),                                                       \
                   (severity),                                                  \
                   nob_sv_from_cstr((component_lit)),                           \
                   (node)->as.cmd.name,                                         \
                   (origin),                                                    \
                   (cause),                                                     \
                   (hint))

#define EVAL_NODE_DIAG(ctx, node, severity, component_lit, cause, hint) \
    EVAL_NODE_ORIGIN_DIAG((ctx),                                         \
                          (node),                                        \
                          eval_origin_from_node((ctx), (node)),          \
                          (severity),                                    \
                          (component_lit),                               \
                          (cause),                                       \
                          (hint))

#define EVAL_DIAG(ctx, severity, component, command, origin, cause, hint) \
    eval_emit_diag((ctx), (severity), (component), (command), (origin), (cause), (hint))

static inline void eval_clear_return_state(Evaluator_Context *ctx) {
    if (!ctx) return;
    ctx->return_requested = false;
    ctx->return_propagate_vars = NULL;
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
Cmake_Event_Origin eval_origin_from_node(const Evaluator_Context *ctx, const Node *node);

// ---- args e expansão (NOVO COMPORTAMENTO) ----
// Retorna a lista expandida e dividida por ';' (alocada na TEMP ARENA)
SV_List eval_resolve_args(Evaluator_Context *ctx, const Args *raw_args);
SV_List eval_resolve_args_literal(Evaluator_Context *ctx, const Args *raw_args);

// ---- diagnostics / events ----
bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev);
bool eval_emit_event(Evaluator_Context *ctx, Cmake_Event ev);
static inline bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    return eval_emit_event(ctx, ev);
}
static inline bool eval_emit_target_prop_set(Evaluator_Context *ctx,
                                             Cmake_Event_Origin origin,
                                             String_View target_name,
                                             String_View key,
                                             String_View value,
                                             Cmake_Target_Property_Op op) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_PROP_SET;
    ev.origin = origin;
    ev.as.target_prop_set.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_prop_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.target_prop_set.value = sv_copy_to_event_arena(ctx, value);
    ev.as.target_prop_set.op = op;
    return emit_event(ctx, ev);
}
static inline bool eval_emit_var_set(Evaluator_Context *ctx,
                                     Cmake_Event_Origin origin,
                                     String_View key,
                                     String_View value) {
    Cmake_Event ev = {0};
    ev.kind = EV_VAR_SET;
    ev.origin = origin;
    ev.as.var_set.key = sv_copy_to_event_arena(ctx, key);
    ev.as.var_set.value = sv_copy_to_event_arena(ctx, value);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_dependency(Evaluator_Context *ctx,
                                               Cmake_Event_Origin origin,
                                               String_View target_name,
                                               String_View dependency_name) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_ADD_DEPENDENCY;
    ev.origin = origin;
    ev.as.target_add_dependency.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_dependency.dependency_name = sv_copy_to_event_arena(ctx, dependency_name);
    return emit_event(ctx, ev);
}
static inline bool eval_emit_target_add_source(Evaluator_Context *ctx,
                                               Cmake_Event_Origin origin,
                                               String_View target_name,
                                               String_View path) {
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_ADD_SOURCE;
    ev.origin = origin;
    ev.as.target_add_source.target_name = sv_copy_to_event_arena(ctx, target_name);
    ev.as.target_add_source.path = sv_copy_to_event_arena(ctx, path);
    return emit_event(ctx, ev);
}
bool eval_emit_diag(Evaluator_Context *ctx,
                    Cmake_Diag_Severity sev,
                    String_View component,
                    String_View command,
                    Cmake_Event_Origin origin,
                    String_View cause,
                    String_View hint);
void eval_diag_classify(String_View component,
                        String_View cause,
                        Cmake_Diag_Severity sev,
                        Eval_Diag_Code *out_code,
                        Eval_Error_Class *out_class);
String_View eval_diag_code_to_sv(Eval_Diag_Code code);
String_View eval_error_class_to_sv(Eval_Error_Class cls);
void eval_report_reset(Evaluator_Context *ctx);
void eval_report_record_diag(Evaluator_Context *ctx,
                             Cmake_Diag_Severity sev,
                             Eval_Diag_Code code,
                             Eval_Error_Class cls);
void eval_report_finalize(Evaluator_Context *ctx);
bool eval_command_caps_lookup(String_View name, Command_Capability *out_capability);
bool eval_append_configure_log(Evaluator_Context *ctx, const Node *node, String_View msg);

// ---- vars ----
String_View eval_var_get(Evaluator_Context *ctx, String_View key);
bool eval_var_defined(Evaluator_Context *ctx, String_View key);
bool eval_var_set(Evaluator_Context *ctx, String_View key, String_View value);
bool eval_var_unset(Evaluator_Context *ctx, String_View key);
bool eval_var_defined_in_current_scope(Evaluator_Context *ctx, String_View key);
bool eval_cache_defined(Evaluator_Context *ctx, String_View key);
bool eval_cache_set(Evaluator_Context *ctx,
                    String_View key,
                    String_View value,
                    String_View type,
                    String_View doc);

static inline String_View eval_current_source_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_SOURCE_DIR));
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_binary_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR));
    if (v.count == 0 && ctx) v = ctx->binary_dir;
    return v;
}

static inline String_View eval_current_list_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_DIR));
    if (v.count == 0 && ctx) v = ctx->source_dir;
    return v;
}

static inline String_View eval_current_list_file(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE));
    if (v.count == 0 && ctx && ctx->current_file) v = nob_sv_from_cstr(ctx->current_file);
    return v;
}

// ---- targets ----
bool eval_target_known(Evaluator_Context *ctx, String_View name);
bool eval_target_register(Evaluator_Context *ctx, String_View name);
bool eval_target_alias_known(Evaluator_Context *ctx, String_View name);
bool eval_target_alias_register(Evaluator_Context *ctx, String_View name);
bool eval_property_define(Evaluator_Context *ctx, const Eval_Property_Definition *definition);
bool eval_property_is_defined(Evaluator_Context *ctx, String_View scope_upper, String_View property_name);
bool eval_target_apply_defined_initializers(Evaluator_Context *ctx, Cmake_Event_Origin origin, String_View target_name);

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
String_View eval_property_store_key_temp(Evaluator_Context *ctx,
                                         String_View scope_upper,
                                         String_View object_id,
                                         String_View prop_upper);
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
bool eval_execute_file(Evaluator_Context *ctx, String_View file_path, bool is_add_subdirectory, String_View explicit_bin_dir);
bool eval_run_ast_inline(Evaluator_Context *ctx, Ast_Root ast);

#ifdef __cplusplus
}
#endif

#endif // EVALUATOR_INTERNAL_H_
