#ifndef EVALUATOR_H_
#define EVALUATOR_H_

// Evaluator v2
// - Input:  Ast_Root (parser.h)
// - Output: Cmake_Event_Stream (append-only)
// - Strict boundary: does NOT include or depend on build_model.h

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"          // String_View
#include "arena.h"        // Arena
#include "parser.h"       // Ast_Root, Node, Args
#include "../transpiler/event_ir.h"

// -----------------------------------------------------------------------------
// Evaluator API
// -----------------------------------------------------------------------------

typedef struct Evaluator_Context Evaluator_Context;

typedef enum {
    EVAL_PROFILE_PERMISSIVE = 0,
    EVAL_PROFILE_STRICT,
    EVAL_PROFILE_CI_STRICT,
} Eval_Compat_Profile;

typedef enum {
    EVAL_ERR_NONE = 0,
    EVAL_ERR_PARSE,
    EVAL_ERR_SEMANTIC,
    EVAL_ERR_UNSUPPORTED,
    EVAL_WARN_LEGACY,
} Eval_Diag_Code;

typedef enum {
    EVAL_ERR_CLASS_NONE = 0,
    EVAL_ERR_CLASS_INPUT_ERROR,
    EVAL_ERR_CLASS_ENGINE_LIMITATION,
    EVAL_ERR_CLASS_IO_ENV_ERROR,
    EVAL_ERR_CLASS_POLICY_CONFLICT,
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
    Arena *arena;        // temporary expansions (rewound frequently)
    Arena *event_arena;  // persistent strings + event stream storage
    Cmake_Event_Stream *stream;

    String_View source_dir; // CMAKE_SOURCE_DIR
    String_View binary_dir; // CMAKE_BINARY_DIR

    const char *current_file; // for origin fallback
    Eval_Compat_Profile compat_profile;
} Evaluator_Init;

Evaluator_Context *evaluator_create(const Evaluator_Init *init);
void evaluator_destroy(Evaluator_Context *ctx);

bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast);
const Eval_Run_Report *evaluator_get_run_report(const Evaluator_Context *ctx);

#endif // EVALUATOR_H_
