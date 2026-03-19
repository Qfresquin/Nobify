#ifndef EVALUATOR_H_
#define EVALUATOR_H_

// Evaluator v2
// - Input:  Ast_Root (parser.h)
// - Output: Event_Stream (append-only semantic IR)
// - Strict boundary: does NOT include or depend on build_model.h

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"          // String_View
#include "arena.h"        // Arena
#include "parser.h"       // Ast_Root, Node, Args
#include "../transpiler/event_ir.h"
#include "eval_diag_registry.h"

// -----------------------------------------------------------------------------
// Evaluator API
// -----------------------------------------------------------------------------

typedef struct EvalSession EvalSession;
typedef struct EvalRegistry EvalRegistry;
typedef struct EvalServices EvalServices;
typedef struct EvalExecContext EvalExecContext;

typedef enum {
    EVAL_RESULT_OK = 0,
    EVAL_RESULT_SOFT_ERROR,
    EVAL_RESULT_FATAL,
} Eval_Result_Kind;

typedef struct {
    Eval_Result_Kind kind;
} Eval_Result;

static inline bool eval_result_is_ok(Eval_Result r) {
    return r.kind == EVAL_RESULT_OK;
}

static inline bool eval_result_is_soft_error(Eval_Result r) {
    return r.kind == EVAL_RESULT_SOFT_ERROR;
}

static inline bool eval_result_is_fatal(Eval_Result r) {
    return r.kind == EVAL_RESULT_FATAL;
}

typedef Eval_Result (*Eval_Native_Command_Handler)(EvalExecContext *exec, const Node *node);

typedef enum {
    EVAL_PROFILE_PERMISSIVE = 0,
    EVAL_PROFILE_STRICT,
    EVAL_PROFILE_CI_STRICT,
} Eval_Compat_Profile;

typedef enum {
#define DECLARE_EVAL_DIAG_CODE(code, text, default_sev, error_class, counts_as_unsupported, hint_contract) code,
    EVAL_DIAG_CODE_LIST(DECLARE_EVAL_DIAG_CODE)
#undef DECLARE_EVAL_DIAG_CODE
} Eval_Diag_Code;

typedef enum {
#define DECLARE_EVAL_ERROR_CLASS(code, text) code,
    EVAL_ERROR_CLASS_LIST(DECLARE_EVAL_ERROR_CLASS)
#undef DECLARE_EVAL_ERROR_CLASS
} Eval_Error_Class;

typedef enum {
    EVAL_UNSUPPORTED_WARN = 0,
    EVAL_UNSUPPORTED_ERROR,
    EVAL_UNSUPPORTED_NOOP_WARN,
} Eval_Unsupported_Policy;

typedef enum {
    EVAL_CMD_IMPL_FULL = 0,
    EVAL_CMD_IMPL_PARTIAL,
    EVAL_CMD_IMPL_MISSING,
} Eval_Command_Impl_Level;

typedef enum {
    EVAL_FALLBACK_NOOP_WARN = 0,
    EVAL_FALLBACK_ERROR_CONTINUE,
    EVAL_FALLBACK_ERROR_STOP,
} Eval_Command_Fallback;

typedef struct {
    String_View command_name;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
} Command_Capability;

typedef struct {
    String_View name;
    Eval_Native_Command_Handler handler;
    Eval_Command_Impl_Level implemented_level;
    Eval_Command_Fallback fallback_behavior;
} EvalNativeCommandDef;

typedef enum {
    EVAL_RUN_OK = 0,
    EVAL_RUN_OK_WITH_WARNINGS,
    EVAL_RUN_OK_WITH_ERRORS,
    EVAL_RUN_FATAL,
} Eval_Run_Overall_Status;

typedef struct {
    size_t warning_count;
    size_t error_count;
    size_t input_error_count;
    size_t engine_limitation_count;
    size_t io_env_error_count;
    size_t policy_conflict_count;
    size_t unsupported_count;
    Eval_Run_Overall_Status overall_status;
} Eval_Run_Report;

typedef struct {
    String_View *argv;
    size_t argc;
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

typedef bool (*Eval_Service_Read_File_Fn)(void *user_data,
                                          Arena *scratch_arena,
                                          String_View path,
                                          String_View *out_contents,
                                          bool *out_found);
typedef bool (*Eval_Service_Write_File_Fn)(void *user_data,
                                           String_View path,
                                           String_View contents,
                                           bool append);
typedef bool (*Eval_Service_Mkdir_Fn)(void *user_data, String_View path);
typedef bool (*Eval_Service_File_Exists_Fn)(void *user_data,
                                            String_View path,
                                            bool *out_exists);
typedef bool (*Eval_Service_Copy_File_Fn)(void *user_data,
                                          String_View src,
                                          String_View dst);
typedef const char *(*Eval_Service_Get_Env_Fn)(void *user_data,
                                               Arena *scratch_arena,
                                               const char *name);
typedef bool (*Eval_Service_Get_Cwd_Fn)(void *user_data,
                                        Arena *scratch_arena,
                                        String_View *out_cwd);
typedef bool (*Eval_Service_Process_Run_Fn)(void *user_data,
                                            Arena *scratch_arena,
                                            const Eval_Process_Run_Request *request,
                                            Eval_Process_Run_Result *out_result);
typedef bool (*Eval_Service_Host_Read_File_Fn)(void *user_data,
                                               Arena *scratch_arena,
                                               String_View path,
                                               String_View *out_contents,
                                               bool *out_found);

struct EvalServices {
    void *user_data;
    Eval_Service_Read_File_Fn fs_read_file;
    Eval_Service_Write_File_Fn fs_write_file;
    Eval_Service_Mkdir_Fn fs_mkdir;
    Eval_Service_File_Exists_Fn fs_file_exists;
    Eval_Service_Copy_File_Fn fs_copy_file;
    Eval_Service_Get_Env_Fn env_get;
    Eval_Service_Get_Cwd_Fn process_get_cwd;
    Eval_Service_Process_Run_Fn process_run_capture;
    Eval_Service_Host_Read_File_Fn host_read_file;
};

typedef struct {
    Arena *persistent_arena;
    const EvalServices *services;
    EvalRegistry *registry; /* optional; default registry if NULL */
    Eval_Compat_Profile compat_profile;
    String_View source_root;
    String_View binary_root;
} EvalSession_Config;

typedef struct {
    Arena *scratch_arena;
    String_View source_dir;
    String_View binary_dir;
    const char *list_file;
    Event_Stream *stream; /* optional */
} EvalExec_Request;

typedef struct {
    Eval_Result result;
    Eval_Run_Report report;
    size_t emitted_event_count;
} EvalRunResult;

EvalSession *eval_session_create(const EvalSession_Config *cfg);
void eval_session_destroy(EvalSession *session);

EvalRunResult eval_session_run(EvalSession *session,
                               const EvalExec_Request *request,
                               Ast_Root ast);

bool eval_session_set_compat_profile(EvalSession *session, Eval_Compat_Profile profile);
bool eval_session_register_native_command(EvalSession *session,
                                          const EvalNativeCommandDef *def);
bool eval_session_unregister_native_command(EvalSession *session,
                                            String_View command_name);
bool eval_session_command_exists(const EvalSession *session, String_View command_name);
bool eval_session_get_visible_var(const EvalSession *session,
                                  String_View key,
                                  String_View *out_value);

EvalRegistry *eval_registry_create(Arena *arena);
void eval_registry_destroy(EvalRegistry *registry);

bool eval_registry_register_native_command(EvalRegistry *registry,
                                           const EvalNativeCommandDef *def);
bool eval_registry_unregister_native_command(EvalRegistry *registry,
                                             String_View command_name);
bool eval_registry_get_command_capability(const EvalRegistry *registry,
                                          String_View command_name,
                                          Command_Capability *out_capability);

const EvalServices *eval_exec_services(const EvalExecContext *exec);
Event_Origin eval_exec_origin_from_node(const EvalExecContext *exec, const Node *node);
bool eval_exec_get_visible_var(const EvalExecContext *exec,
                               String_View key,
                               String_View *out_value);
bool eval_exec_set_current_var(EvalExecContext *exec,
                               String_View key,
                               String_View value);
bool eval_exec_unset_current_var(EvalExecContext *exec, String_View key);
Eval_Result eval_exec_emit_diag(EvalExecContext *exec,
                                Event_Diag_Severity severity,
                                Eval_Diag_Code code,
                                String_View component,
                                String_View command,
                                Event_Origin origin,
                                String_View cause,
                                String_View hint);

#endif // EVALUATOR_H_
